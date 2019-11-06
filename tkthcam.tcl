#!/usr/bin/wish8.6
# TODO:
#       load settings from sensor (GetSetting)
#       shutter temp support (SetShutterTemperature)?

#lappend ::tcl::Path Release_VC1900

namespace import tcl::mathop::*
::tcl::tm::path add .
package require imgtools

set config_file "~/.tkthcam.cfg"
set config_vars {interpolation gradient palette_remap ruler_range rawtemp 
                 temp_off}
set config_vars_teq1 {vmode emissivity}
set config_vars_lepton3 {}

# defaults
set config_teq1(vmode) 1
set config_teq1(emissivity) 100 ;# FIXME: for some reason it fails if set to 30

set temp_off 0.0
set emissivity 100
set img_scale 1.0
set interpolation linear
set gradient gradients/rainbow.ppm
set palette_remap off
set ruler_range limits
set clip_min ""
set clip_max ""
set temp_lock off

set radiometry 0
set gain 0
set vmode 1
set rawtemp 0

set debug 0

# temperature calibration
set lepton3_cal_high_offset 1539
set lepton3_cal_high_scale  0.015126
set lepton3_cal_low_offset  3238
set lepton3_cal_low_scale   0.060160

# float->uint16 encoding for te
set teq1_scale  [expr {400.0/65535}]
set teq1_offset [expr {50.0/$teq1_scale}]


if {[file readable $config_file]} {
 source $config_file
}

proc set_sensor_type {type} {
 switch -exact $type {
  lepton3 {
   set ::tconv lepton3_tconv
   set ::img_width 160
   set ::img_height 120
   set frame_sz [expr {164*120}]
   set ::frame_sz_bytes [expr {$frame_sz*2}]
   set ::sensor_drv "sudo ./lepton3drv"
   set ::sensor_menu .m.sm_lepton
   set ::max_raw_temp 16383
   set ::start_rec_file start_rec_file_lepton3
   set ::img_format lepton3raw
   namespace import -force thermImg::lepton3::*
  }
  teq1 {
   set ::tconv teq1_tconv
   set ::img_width 384
   set ::img_height 288
   set frame_sz [expr {$::img_width*$::img_height}]
   set ::frame_sz_bytes [expr {$frame_sz*2}]
   set ::sensor_drv ./teq1drv
   set ::sensor_menu .m.sm_teq1
   set ::max_raw_temp 65535
   set ::start_rec_file start_rec_file_teq1
   set ::img_format teq1raw
   namespace import -force ::thermImg::teq1::*
  }
 }
}

proc load_sensor_config {s} {
 set v [set ::config_vars_$s]
 foreach i $v {
  set ::$i [set ::config_${s}($i)]
  switch $i {
   vmode {set_vmode}
   emissivity {do_update_emissivity}
  }
 }
}

proc update_img {} {
 global interpolation img_width img_height img_scale
 if {$interpolation ne "int"} {
  set scale [expr {$::img_scale*100}]%
  ::imgtools::scale frame_img $scale -interpolation $interpolation frame_img_scaled
  #::imgtools::scale frame_img [expr {int($img_width*$img_scale)}]x[expr {int($img_height*$img_scale)}] -interpolation $interpolation frame_img_scaled
 } else {
  set scale [expr {int($::img_scale)}]
  frame_img_scaled copy frame_img -zoom $scale
 }
}

set display_task ""
proc display_frame {} {
 global display_task
 if {$display_task ne ""} return
 set display_task [after idle do_display_frame]
}

proc lepton3_tconv {t} {
 upvar #0 lepton3_cal_high_offset hoff
 upvar #0 lepton3_cal_high_scale hscale
 upvar #0 lepton3_cal_low_offset loff
 upvar #0 lepton3_cal_low_scale lscale

 if {$::gain == 0} {
  set t [expr {($t-$hoff)*$hscale}]
 } else {
  set t [expr {($t-$loff)*$lscale}]
 }
 return [format %0.2f $t]
}

proc teq1_tconv {t} {
 # TODO: use local emissivity correction if it does the same as the teq1 driver
 # set e $::config_teq1(emissivity)
 #return [format %0.2f [expr {(($t-$::teq1_offset)*$::teq1_scale + 273.15)/pow($e/100.0, 0.25) - 273.15 + $::temp_off}]]
 return [format %0.2f [expr {($t-$::teq1_offset)*$::teq1_scale + $::temp_off}]]
}

set rec_fd ""
set last_frame ""
proc process_frame {f} {
 global rec_fd
 if {$rec_fd ne ""} {
  puts -nonewline $rec_fd $f
 }
 set ::last_frame $f
 display_frame
}

proc clip_bound {clip val} {
     if {$clip ne ""} {
       return $clip
     }
     return $val
}

proc ruler_show_temp {min max} {
 if {$::radiometry != 0 && !$::rawtemp} {
  set min [$::tconv $min]
  set max [$::tconv $max]
 }
 $::tmin configure -text $min
 $::tmax configure -text $max
}

set force_update_ruler 1
proc update_ruler {minmax} {
 set force $::force_update_ruler
 set ::force_update_ruler 0
 if {$::ruler_range eq "off"} {
  if {$force} {
   gradient_img_scaled blank
   $::tmin configure -text "---"
   $::tmax configure -text "---"
  }
  return
 }

 if {$::palette_remap eq "off" && (($::ruler_range eq "minmax" && $::clip_min eq "" && $::clip_max eq "") 
                                 || $::ruler_range eq "limits")} {
  if {$force} {
    fillPaletteImg direct gradient_img_scaled
  }
 } elseif {$::ruler_range eq "full" && $::palette_remap ne "off"} {
  if {$force || $::palette_remap eq "on"} {
   fillPaletteImg map gradient_img_scaled
  }
 } elseif {$::palette_remap eq "once" && $::ruler_range eq "limits" && $::clip_min ne "" && $::clip_max ne ""} {
  if {$force} {
   fillPaletteImg scalemap gradient_img_scaled $::clip_min $::clip_max 0 $::max_raw_temp
  }
 } else {
  if {$minmax eq ""} {set minmax [getRange $::last_frame]}
  set clipped_min [clip_bound $::clip_min [lindex $minmax 0]]
  set clipped_max [clip_bound $::clip_max [lindex $minmax 1]]
 
  # dst_min, dst_max = ruler range
  # src_min, src_max = temperature range of the whole palette
 
  if {$::palette_remap ne "off"} {
   set cmd scalemap
   set src_min 0
   set src_max $::max_raw_temp
  } else {
   set cmd scale
   set src_min $clipped_min
   set src_max $clipped_max
  }
  switch -exact $::ruler_range {
   full {
    set dst_min 0
    set dst_max $::max_raw_temp
   }
   limits {
    set dst_min $clipped_min
    set dst_max $clipped_max
   }
   minmax {
    set dst_min [lindex $minmax 0]
    set dst_max [lindex $minmax 1]
   }
  }
  fillPaletteImg $cmd gradient_img_scaled  $dst_min $dst_max $src_min $src_max
 }

 switch -exact $::ruler_range {
   full {
    if {$force} {ruler_show_temp 0 $::max_raw_temp}
   }
   limits {
    if {$::clip_max ne "" && $::clip_min ne ""} {
     if {$force} {
      ruler_show_temp $::clip_min $::clip_max
     }
    } else {
     if {$minmax eq ""} {set minmax [getRange $::last_frame]}
     ruler_show_temp [clip_bound $::clip_min [lindex $minmax 0]] [clip_bound $::clip_max [lindex $minmax 1]]
    }
   }
   minmax {
    if {$minmax eq ""} {set minmax [getRange $::last_frame]}
    ruler_show_temp [lindex $minmax 0] [lindex $minmax 1]
   }
 }
}

proc do_display_frame {} {
 upvar #0 last_frame f
 set ::display_task ""
 if {$f eq ""} {
  update_ruler [list 0 $::max_raw_temp]
  return
 }

 set minmax ""
 set clip_min $::clip_min
 set clip_max $::clip_max
 switch -exact $::palette_remap {
  on {
   if {$clip_min eq ""} {set clip_min 0}
   if {$clip_max eq ""} {set clip_max $::max_raw_temp}
   set minmax [remapPalette stepped $f $clip_min $clip_max]
   #frame_img put @$f -format "$::img_format map"
   fillImg frame_img map $f
  }
  once {
   #FIXME: add clipping?
   #if {$clip_min eq ""} {set clip_min 0}
   #if {$clip_max eq ""} {set clip_max $::max_raw_temp}
   frame_img put @$f -format "$::img_format map"
  }
  default {
   set mode scale
   if {$clip_max eq "" || $clip_max eq ""} {
    set minmax [getRange $f]
    if {$clip_min eq ""} {set clip_min [lindex $minmax 0]}
    if {$clip_max eq ""} {set clip_max [lindex $minmax 1]}
   }
   frame_img put @$f -format "$::img_format scale $clip_min $clip_max"
  }
 }
 update_ruler $minmax
 marker::update
 update_img
}

proc process_vstream {} {
 global vframe
 global frame_sz_bytes

 set bytes [read $::vofd $frame_sz_bytes]
 append vframe $bytes
 set l [string length $vframe]
 #puts "$l [string length $bytes]"
 if {$l == $frame_sz_bytes} {
  process_frame $vframe
  set vframe ""
 } elseif {$l > $frame_sz_bytes} {
  binary scan $vframe a${frame_sz_bytes}a* b vframe
  process_frame $b
 } elseif {![string length $bytes]} {
  if {$::play_file eq ""} {
   puts stderr "error: read failed"
   exit 2
  } else {
   after cancel file_timer
   .bf.play configure -text Resume
  }
 }
}

set resize_task ""
proc delayed_resize {} {
 global resize_task
 if {$resize_task eq ""} {set resize_task [after 100 resize_new]}
}

set sash_off 4
proc resize_new {} {
 global img_scale img_width img_height
 set ::resize_task ""
 set hsize [lindex [.p sash coord 0] 1]
 incr hsize -$::sash_off
 #puts $hsize
 set img_height_f $img_height.0
 set s1 [expr {$hsize/$img_height_f}]
 set s $s1

 if {$s > 5} {set s 5.0}
 if {$s < 1} {set s 1.0}
 if {$::interpolation eq "int"} {
  set s [expr {int($s1)}]
 }

 if {$s == $img_scale && $s1 == $img_scale} return

 if {$::interpolation eq "int"} {
   set h [expr {int($img_height*$s)}]
   incr h $::sash_off
  .p sash place 0 0 $h
 }
 if {$s == $img_scale} return

 set w [expr {int($img_width*$s)}]
 set h [expr {int($img_height*$s)}]
 $::c configure -width $w
 $::c configure -height $h
 $::g configure -height $h

 set img_scale $s
 frame_img_scaled blank
 gradient_img_scaled blank
 #::imgtools::scale gradient_img 15x[expr {int($img_height*$img_scale)}] gradient_img_scaled
 #gradient_img_scaled config -width 15 -height [expr {int($img_height*$img_scale)}]
 gradient_img_scaled config -width 15 -height $h
 $::fp config -length $w
 set ::force_update_ruler 1
 display_frame
}

set l3rw_magic "TKTHL3RW"
set l3rw_ver 1
set teq1rw_magic "TKTHQ1RW"
set teq1rw_ver 1

proc start_rec_file_lepton3 f {
 set h [binary format a8cucucudddd@[expr {$::frame_sz_bytes-1}]c \
                      $::l3rw_magic $::l3rw_ver $::gain $::radiometry \
                      $::lepton3_cal_high_offset $::lepton3_cal_high_scale \
                      $::lepton3_cal_low_offset $::lepton3_cal_low_scale 0]
 set fd [open $f w]
 fconfigure $fd -translation binary
 puts -nonewline $fd $h
 return $fd
}

proc start_rec_file_teq1 f {
 set h [binary format a8cucucu@[expr {$::frame_sz_bytes-1}]c \
                      $::teq1rw_magic $::teq1rw_ver $::vmode $::radiometry 0]
 set fd [open $f w]
 fconfigure $fd -translation binary
 puts -nonewline $fd $h
 return $fd
}

proc open_rec_file f {
 set sz [file size $f]
 set vofd [open $f r]
 fconfigure $vofd -translation binary
 set fr [read $vofd 8]
 if {[string length $fr] != 8} {
  puts stderr "error: \"$f\" is too small"
  exit 1
 }
 binary scan $fr a8 magic v g r
 set plain 0
 switch -- $magic  \
  $::l3rw_magic { set_sensor_type lepton3 } \
  $::teq1rw_magic { set_sensor_type teq1 } \
  default {set plain 1}
 
 if {!$plain} {
  set rem [expr {$::frame_sz_bytes - 8}]
  set fr [read $vofd $rem]
  if {[string length $fr] != $rem} {
   puts stderr "error: \"$f\" is too small"
   exit 1
  }
  binary scan $fr ccc v g r
 }
 
 switch -- $magic \
  $::l3rw_magic { 
   set ::radiometry $r
   set ::gain $g
   set ::rec_file_start $::frame_sz_bytes
   incr sz -$::frame_sz_bytes } \
  $::teq1rw_magic { 
   set ::vmode $g
   set ::radiometry $r
   set ::rec_file_start $::frame_sz_bytes } \
  default {
   # assume plain raw file
   seek $vofd 0
   set ::rec_file_start 0
  }

 set sz_sec [expr {$sz/$::frame_sz_bytes/10.0}]
 set ::rec_file_time [format "%02d:%02d" [expr {int($sz_sec)/60}] [expr {int($sz_sec)%60}]]
 $::fp configure -to $sz_sec

 return $vofd
}

proc file_timer {} {
 upvar #0 rec_file_fpos fp
 set fp [expr {$fp + 0.1}]
 set ::rec_file_tpos [format "%02d:%02d/$::rec_file_time" [expr {int($fp)/60}] [expr {int($fp)%60}]]
 after 100 file_timer
 process_vstream
}

proc start_playback f {
 set ::vofd [open_rec_file $f]
 set_gradient
 if {[.m entrycget Sensor -menu] ne $::sensor_menu} {
  .m entryconfigure Sensor -menu $::sensor_menu
 }
 file_timer
}

proc start_rec {} {
 global rec_fd
 set fn [clock format [clock seconds] -format %Y-%m-%d-%H:%M:%S.traw]
 set rec_fd [$::start_rec_file $fn]
 .bf.rec configure -text "Stop" -command stop_rec
}

proc stop_rec {} {
 close $::rec_fd
 set ::rec_fd ""
 .bf.rec configure -text "Rec" -command start_rec
}

proc remap_colors {} {
 switch -exact $::palette_remap {
   once {remapPalette linear $::last_frame [clip_bound $::clip_min 0] [clip_bound $::clip_max $::max_raw_temp]}
   off {}
   linear {}
 }
 redisplay
}

proc set_gain {} {
 puts $::vofd "g $::gain"; flush $::vofd
}

proc set_vmode {} {
 puts $::vofd "s $::vmode"; flush $::vofd
 set ::config_${::sensor}(vmode) $::vmode
 if {$::sensor eq "teq1"} {
  set ::radiometry [expr {$::vmode >= 4}]
 }
}

proc save_config {} {
 set fd [open $::config_file w]
 foreach i $::config_vars {
  puts $fd "set $i [set ::$i]"
 }
 foreach i $::config_vars_teq1 {
  puts $fd "set config_teq1($i) [set ::config_teq1($i)]"
 }
 foreach i $::config_vars_lepton3 {
  puts $fd "set config_lepton3($i) [set ::config_lepton3($i)]"
 }
 #set img_scale 1.0
 #set clip_min ""
 #set clip_max ""
 #set temp_lock off

#set radiometry 0
#set gain 0
#set vmode 1
#set rawtemp 0
 close $fd
}

namespace eval marker {
 variable markers
 proc update {} {
  variable markers
  dict for {id mk} $markers {
   [dict get $mk type]::update $id $mk
  }
 }

 proc alloc_id {} {
  variable markers
  for {set i 1} {$i <= 7} {incr i} {
   if {![dict exists $markers $i]} {return $i}
  }
  return 0
 }

 proc win2frame {wx wy} {
  set x [expr {int($wx / $::img_scale)}]
  set y [expr {int($wy / $::img_scale)}]
  if {$x >= $::img_width} {set x [- $::img_width 1]}
  if {$y >= $::img_height} {set y [- $::img_height 1]}
  return [list $x $y]
 }

 proc frame2win {fx fy} {
  set x [expr {int($fx * $::img_scale)}]
  set y [expr {int($fy * $::img_scale)}]
  return [list $x $y]
 }
}

namespace eval marker::cursor {
 set cx ""
 set cy ""

 proc handle_move {wx wy} {
  variable cx
  variable cy

  if {$::last_frame eq ""} return
  lassign [marker::win2frame $wx $wy] cx cy
  update 0 ""
 }

 proc update {id desc} {
  variable cx
  variable cy
 
  if {$::last_frame eq ""} return
  if {$cx eq ""} return
 
  #FIXME: lepton
  set pos [expr {($cy * $::img_width + $cx)*2}]
  binary scan $::last_frame "@${pos}su" t
  if {$::radiometry != 0 && !$::rawtemp} {
   set t [$::tconv $t]
  }
  $::tcur configure -text "C($cx, $cy)\n  $t"
 }

 proc add {} {
  dict set d type cursor
  dict set marker::markers 0 $d
  bind $::c "<Motion>" "+marker::cursor::handle_move %x %y"
 }

 proc delete {id} {
  binding_del $::c "<Motion>" marker::cursor::handle_move
  dict unset marker::markers 0
  $::tcur configure -text ""
 }
}

proc binding_del {win ev prefix} {
 set b [bind $win $ev]
 set ch [string first $prefix $b]
 set ch2 [string first "\n" $b $ch]
 if {$ch2 == -1} {
  set ch2 end
  if {$ch != 0} {incr ch -1}
 }
 bind $win $ev [string range $b 0 $ch-1][string range $b $ch2+1 end]
}

namespace eval marker::box {
 variable box_selection 0

 proc drag {x y} {
  variable sx
  variable sy
  variable canvas_rect1
  variable canvas_rect2
  $::c coords $canvas_rect1 $sx $sy $x $y
  incr x
  incr y
  $::c coords $canvas_rect2 [+ $sx 1] [+ $sy 1] $x $y
 } 

 proc label {desc} {
  lassign [dict get $desc fbox] x1 y1 x2 y2
  set w [- $x2 $x1 -1]
  set h [- $y2 $y1 -1]
  return "R($x1, $y1, ${w}x${h})"
 }

 proc stop_drag {x y} {
  variable sx
  variable sy
  variable canvas_rect1
  variable canvas_rect2
  variable box_selection

  $::c coords $canvas_rect1 $sx $sy $x $y
  $::c coords $canvas_rect2 [+ $sx 1] [+ $sy 1] [+ $x 1] [+ $y 1]
  $::c itemconfigure $canvas_rect1 -outline red
  $::c itemconfigure $canvas_rect2 -outline green
  binding_del $::c "<Motion>" marker::box::drag
  bind $::c <Button-1> {}
  set box_selection 0
  dict set b r1 $canvas_rect1
  dict set b r2 $canvas_rect2
  dict set b wbox [list $sx $sy $x $y]
  dict set b fbox [concat [marker::win2frame $sx $sy] [marker::win2frame $x $y]]
  dict set b type box
  dict set b scale $::img_scale
  set id [marker::alloc_id]
  dict set marker::markers $id $b
  $::tm entryconfigure Delete -state normal
  $::tm.delete add command -label [label $b] -command "marker::box::delete $id"
  update $id $b
 }

 proc start_drag {x y} {
  variable sx $x
  variable sy $y
  variable canvas_rect1
  variable canvas_rect2

  bind $::c <Button-1> {marker::box::stop_drag %x %y}
  bind $::c <Motion> "+marker::box::drag %x %y"
  set canvas_rect1 [$::c create rectangle $x $y $x $y]
  incr x
  incr y
  set canvas_rect2 [$::c create rectangle $x $y $x $y -outline white]
 }

 proc update {id desc} {
  if {$::last_frame eq ""} return

  lassign [dict get $desc fbox] x1 y1 x2 y2
  set w [- $x2 $x1 -1]
  set h [- $y2 $y1 -1]
  set pos [* [+ [* $y1 $::img_width] $x1] 2]
  set min 65536
  set max -1
  set sum 0
  for {set y $y1} {$y <= $y2} {incr y} {
   binary scan $::last_frame "@${pos}su$w" t
   foreach v $t {
    if {$v < $min} {set min $v}
    if {$v > $max} {set max $v}
    set sum [+ $sum $v]
   }
   incr pos $::img_width
   incr pos $::img_width
  }
  set avg [/ $sum.0 $w $h]
  if {$::radiometry != 0 && !$::rawtemp} {
   set min [$::tconv $min]
   set max [$::tconv $max]
   set avg [$::tconv $avg]
  }
  $::mkr$id configure -text "R($x1, $y1, ${w}x${h})\n  $min < $avg < $max"
  if {[dict get $desc scale] != $::img_scale} {
   # resize
   lassign [marker::frame2win $x1 $y1] sx sy
   lassign [marker::frame2win $x2 $y2] x y
   $::c coords [dict get $desc r1] $sx $sy $x $y
   $::c coords [dict get $desc r2] [+ $sx 1] [+ $sy 1] [+ $x 1] [+ $y 1]
   dict set desc scale $::img_scale
   dict set marker::markers $id $desc
  }
 }

 proc delete {id} {
  $::tm.delete delete [label [dict get $marker::markers $id]]
  if {[$::tm.delete index end] eq "none"} {
   $::tm entryconfigure "Delete" -state disabled
  }
  $::c delete [dict get $marker::markers $id r1] [dict get $marker::markers $id r2]
  $::mkr$id configure -text ""
  dict unset marker::markers $id
 }

 proc add {} {
  variable box_selection
  if {$box_selection} return
  set box_selection 1
  bind $::c <Button-1> {marker::box::start_drag %x %y}
 }
}

image create photo frame_img
image create photo frame_img_scaled
image create photo gradient_img
image create photo gradient_img_scaled

# UI vars
set player 0
set c .p.f1.c
set g .p.f1.g
set dm .m.dm
set tm .m.tm
set tmin .p.f1.t_min
set tmax .p.f1.t_max
set tcur .p.f1.t_cur
set mkr .p.f1.t_mkr
set vm_teq1 .m.sm_teq1.vm
set vm_lepton .m.sm_lepton.vm
set space_col 4
set space_row 4

set play_file ""
set sensor teq1
for {set i 0} {$i < [llength $argv]} {incr i} {
 set v [lindex $argv $i]
 switch -- $v {
  -debug {
   set debug 1
  }
  -sensor {
   incr i
   set sensor [lindex $argv $i]
  }
  -- {
   incr i
   set play_file [lindex $argv $i]
   break
  }
  default {
   set play_file $v
  }
 }
}
if {$play_file ne ""} {
 set player 1
}

if {$::tcl_platform(os) eq "Windows NT"} {
  load win/Release_VC1900/thermimg01t.dll
} else {
  load ./thermImg.so
}
namespace eval ::thermImg::lepton3 { namespace export * }
namespace eval ::thermImg::teq1 { namespace export * }

set_sensor_type $sensor

proc validate_float_range {win min max val var} {
 if {[string is double $val]} {
  if {$val >= $min && $val <= $max} {
   upvar #0 $var v
   set v $val
   $win configure -bg white
   return 1
  }
 }
 $win configure -bg red
 return 1
}

proc do_update_emissivity {} {
 set ::update_emissivty_task ""
 if {$::play_file eq ""} {
  puts $::vofd "e $::config_teq1(emissivity)"
  flush $::vofd
 }
}
set update_emissivty_task ""
proc update_emissivty {val} {
 global update_emissivty_task
 set ::config_teq1(emissivity) $::emissivity
 if {$update_emissivty_task ne ""} return
 set update_emissivty_task [after 700 do_update_emissivity]
}

proc spawn_temp_corr {} {
 if {[info command .tc] ne ""} return
 toplevel .tc
 button .tc.done -text Done -command {destroy .tc}
 label .tc.offs -text Offset:
 #scale .tc.offs -from -25 -to 25 -label Offset: \
         -orient horiz -showvalue 0 -variable temp_off -resolution 0.1
 entry .tc.offe -width 8 -validate key -vcmd "validate_float_range .tc.offe -25 25 %P temp_off"
 .tc.offe insert end $::temp_off
 label .tc.emis -text Emissivity:
 #scale .tc.emis -from 0 -to 100 -label Emissivity: \
         -orient horiz -showvalue 0 -variable emissivity -resolution 1 -command {update_emissivty}
 entry .tc.emie -width 8 -validate key -vcmd "validate_float_range .tc.emie 50 100 %P emissivity"
 .tc.emie insert end $::emissivity

 grid .tc.offs -sticky e -row 1 -column 1
 grid .tc.offe -sticky w -row 1 -column 2 -pady 1
 grid .tc.emis -sticky e -row 2 -column 1
 grid .tc.emie -sticky w -row 2 -column 2 -pady 1
 label .tc.spacer
 #grid columnconfigure .tc 1 -weight 1
 #grid rowconfig .tc 3 -weight 1
 grid .tc.spacer -columnspan 2 -column 1 -row 3
 grid .tc.done -columnspan 2 -column 1 -row 4
}

package require Img
proc CaptureWindow {win {baseImg ""} {px 0} {py 0}} {
   # create the base image of win (the root of capturing process)
   if {$baseImg eq ""} {
     set baseImg [image create photo -format window -data $win]
   }
   # paste images of win's children on the base image
   foreach child [winfo children $win] {
     if {![winfo ismapped $child]} continue
     set childImg [image create photo -format window -data $child]
     regexp {\+(\d*)\+(\d*)} [winfo geometry $child] -> x y
     $baseImg copy $childImg -to [incr x $px] [incr y $py]
     image delete $childImg
     CaptureWindow $child $baseImg $x $y
   }
   return $baseImg
}

proc save_screenshot {} {
 set img [CaptureWindow .p.f1]
 set b [grid bbox .p.f1 1 1 4 10]
 $img configure -width [lindex $b 2] -height [lindex $b 3]
 if {$::play_file eq ""} {
  set name [clock format [clock seconds] -format "screenshot-thermal-%Y-%m-%d-%H_%M_%S.png"]
 } else {
  set f [file rootname $::play_file]
  set name screenshot-thermal-$f-
  set fp [expr {int($::rec_file_fpos)}]
  append name [format "%02d_%02d.png" [/ $fp 60] [% $fp 60]]
 }

 $img write -format png $name
 image delete $img
}

panedwindow .p -orient vertical  -showhandle 1 -sashrelief sunken -opaqueresize 0 -background black
frame .p.f1
canvas $c -background black -highlightbackground black -height $::img_height -highlightthickness 1
canvas $g -width 15 -background black -highlightbackground black -highlightthickness 1
$g create image 0 0 -image gradient_img_scaled -anchor nw
label $tmax -text $::max_raw_temp -borderwidth 3
label $tmin -text 0 -borderwidth 3
label $tcur -text "" -borderwidth 3 -justify left
for {set i 1} {$i <= 7} {incr i} {
 label $mkr$i -text "" -borderwidth 3 -justify left
}

frame .bf
button .bf.ffc -text FFC -command {puts $vofd "f $radiometry"; flush $vofd}
button .bf.snap -text Photo -command save_screenshot
proc lock_temp {update} {
 global clip_min clip_max
 global temp_lock

 if {$::last_frame eq ""} return
 set r [getRange $::last_frame]
 if {$temp_lock eq "off"} {
  set clip_max [lindex $r 1] 
  set clip_min [lindex $r 0] 
  set temp_lock minmax
 } else {
  if {$temp_lock eq "min" || $temp_lock eq "minmax"} {
   if {$update || $clip_min eq ""} {set clip_min [lindex $r 0]}
  } else {
   set clip_min ""
  }
  if {$temp_lock eq "max" || $temp_lock eq "minmax"} {
   if {$update || $clip_max eq ""} {set clip_max [lindex $r 1]}
  } else {
   set clip_max ""
  }
 }
 if {$::palette_remap eq "once"} {
  remap_colors
 } else {
  redisplay
 }
}

button .bf.rlock -text Lock -command {lock_temp 1}
button .bf.reset -text Reset -command {puts $vofd r; flush $vofd}
button .bf.rec -text Rec -command start_rec
button .bf.play -text Pause -command {
  if {[.bf.play cget -text] eq "Pause"} {
   .bf.play configure -text Resume
   after cancel file_timer
  } else {
   .bf.play configure -text Pause
   after 1 file_timer
  }
}
#foreach i {rlock reset ffc rec play snap} {
# .bf.$i config -highlightthickness 2 -relief flat -highlightbackground black
#}
menu .m -relief flat

#menubutton .bf.display -text Display -indicatoron 1 -menu .bf.display.m
#menubutton .bf.sensor -text Sensor -indicatoron 1 -menu .bf.sensor.m
menu .m.sm_lepton
menu .m.sm_teq1
menu $vm_lepton
menu $vm_teq1
menu $dm
menu $dm.ip
menu $dm.g
menu $dm.gm
menu $dm.tl
menu $dm.tr
menu $tm

$dm add cascade -label "Interpolation" -menu $dm.ip 
$dm add cascade -label "Palette" -menu $dm.g 
$dm add cascade -label "Remap Colors" -menu $dm.gm
$dm add cascade -label "Temp Ruler" -menu $dm.tr
$dm add cascade -label "Temp Lock" -menu $dm.tl
$dm add checkbutton -label "Raw values" -onvalue 1 -offvalue 0 -variable rawtemp

if {$player} {
 set smmenu_state disabled
} else {
 set smmenu_state normal
}
.m.sm_teq1 add cascade -label "Video Select" -menu $vm_teq1
.m.sm_lepton add cascade -label "Video Select" -menu $vm_lepton
.m.sm_lepton add checkbutton -label "Radiometry" -state $smmenu_state -onvalue 1 -offvalue 0 -variable radiometry -command {puts $vofd "l $radiometry"; flush $vofd}
.m.sm_lepton add checkbutton -label "Low Gain" -state $smmenu_state -onvalue 1 -offvalue 0 -variable gain -command set_gain
.m.sm_teq1 add command -label "Update Dead" -command {puts $vofd "d"; flush $vofd}
proc redisplay {} {
 set ::force_update_ruler 1
 display_frame
}

$dm.gm add radiobutton -label "Off" -value off -variable palette_remap -command redisplay
$dm.gm add radiobutton -label "Last Frame" -value once -variable palette_remap -command remap_colors
$dm.gm add radiobutton -label "Every Frame" -value on -variable palette_remap -command redisplay

$dm.tr add radiobutton -label "Off" -value off -variable ruler_range -command redisplay
$dm.tr add radiobutton -label "Full Range" -value full -variable ruler_range -command redisplay
$dm.tr add radiobutton -label "Limits" -value limits -variable ruler_range -command redisplay
$dm.tr add radiobutton -label "Frame Range" -value minmax -variable ruler_range -command redisplay

$dm.tl add radiobutton -label "Off" -value off -variable temp_lock -command {set clip_min ""; set clip_max ""; redisplay}
$dm.tl add radiobutton -label "Min" -value min -variable temp_lock -command {lock_temp 0}
$dm.tl add radiobutton -label "Max" -value max -variable temp_lock -command {lock_temp 0}
$dm.tl add radiobutton -label "Min+Max" -value minmax -variable temp_lock -command {lock_temp 0}

$dm.ip add radiobutton -label Integer -value int -variable interpolation -command delayed_resize
$dm.ip add radiobutton -label Linear  -value linear -variable interpolation -command delayed_resize
$dm.ip add radiobutton -label Cubic -value catrom -variable interpolation -command delayed_resize
$dm.ip add radiobutton -label Sinc1 -value lanczos2 -variable interpolation -command delayed_resize
$dm.ip add radiobutton -label Sinc2 -value lanczos3 -variable interpolation -command delayed_resize

set temp_at_cursor 1
$tm add checkbutton -label "Cursor" -variable temp_at_cursor -command {
                                       if {$temp_at_cursor} {
                                        marker::cursor::add
                                       } else {
                                        marker::cursor::delete 0
                                       }
                                      }
$tm add command -label "Add Rect" -command {marker::box::add}
menu $tm.delete -tearoff 0
$tm add cascade -label "Delete" -menu $tm.delete -state disabled
$tm add command -label "Correction" -command {spawn_temp_corr}

$vm_lepton add radiobutton -label Raw -state $smmenu_state -value 0 -variable vmode -command set_vmode
$vm_lepton add radiobutton -label Normal -state $smmenu_state -value 1 -variable vmode -command set_vmode
$vm_lepton add radiobutton -label Ramp -state $smmenu_state -value 2 -variable vmode -command set_vmode
$vm_lepton add radiobutton -label Const -state $smmenu_state -value 3 -variable vmode -command set_vmode
$vm_lepton add radiobutton -label RampH -state $smmenu_state -value 4 -variable vmode -command set_vmode
$vm_lepton add radiobutton -label RampV -state $smmenu_state -value 5 -variable vmode -command set_vmode
$vm_lepton add radiobutton -label RampC -state $smmenu_state -value 6 -variable vmode -command set_vmode
$vm_lepton add radiobutton -label Average -state $smmenu_state -value 7 -variable vmode -command set_vmode
$vm_lepton add radiobutton -label Freeze -state $smmenu_state -value 8 -variable vmode -command set_vmode
$vm_lepton add radiobutton -label F0 -state $smmenu_state -value 9 -variable vmode -command set_vmode
$vm_lepton add radiobutton -label F1 -state $smmenu_state -value 10 -variable vmode -command set_vmode
$vm_lepton add radiobutton -label F2 -state $smmenu_state -value 11 -variable vmode -command set_vmode
$vm_lepton add radiobutton -label F3 -state $smmenu_state -value 12 -variable vmode -command set_vmode
$vm_lepton add radiobutton -label F4 -state $smmenu_state -value 13 -variable vmode -command set_vmode

$vm_teq1 add radiobutton -label Int16 -state $smmenu_state -value 0 -variable vmode -command {set_vmode; set radiometry 0}
$vm_teq1 add radiobutton -label Float -state $smmenu_state -value 3 -variable vmode -command {set_vmode; set radiometry 0}
$vm_teq1 add radiobutton -label Temp -state $smmenu_state -value 4 -variable vmode -command {set_vmode; set radiometry 1}
$vm_teq1 add radiobutton -label Temp1 -state $smmenu_state -value 6 -variable vmode -command {set_vmode; set radiometry 1}

proc rec_file_set_pos {pos} {
 seek $::vofd [expr {int($pos*10)*$::frame_sz_bytes+$::rec_file_start}]
}
set fp .bf.pf.filepos
set ft .bf.pf.ft
frame .bf.pf
scale $fp -width 10 -resolution 0.1 -orient horizontal -showvalue false -command rec_file_set_pos -variable rec_file_fpos
label $ft -textvar rec_file_tpos
pack $fp -side left
pack $ft -side left

set n 0
foreach i [glob gradients/*.ppm] {
                           image create photo tmp -file $i -format ppm
                           image create photo gr$n
                           ::imgtools::scale tmp 40x10 gr$n 
                           $dm.g add radiobutton -compound left -label [file rootname [file tail $i]] -image gr$n -value $i -variable gradient -command {
                           set_gradient; redisplay }
                           image delete tmp
                           incr n
                           }

.p add .p.f1
.p add .bf
grid $c -row 1 -column 1 -rowspan 10 -sticky nwse
grid $g -row 1 -column 2 -rowspan 10 -sticky nws
grid $tmax -row 1 -sticky wn -column 3
grid $tmin -row 10 -sticky ws -column 3
grid $tcur -row 2 -sticky wn -column 4
for {set i 3; set j 1} {$i < 10} {incr i; incr j} {
 grid $mkr$j -row $i -sticky wn -column 4
}
if {!$player} {
 grid .bf.ffc -row 2 -column 1
 grid .bf.reset -row 2 -column 5
} else {
 grid .bf.play -row 2 -column 1
 grid .bf.pf -row 1 -column 1 -columnspan 10 -sticky we
 #grid .bf.filepos -row 1 -column 1 -columnspan 10 -sticky w
 #grid .bf.filetime -row 1 -column 11 -sticky w
 grid columnconfigure .bf 10 -weight 1
 #grid columnconfigure .bf 11 -weight 100
}
grid .bf.snap -row 2 -column 2
grid .bf.rec -row 2 -column 3
grid .bf.rlock -row 2 -column 4
#grid .bf.display -row 2 -column 6
#grid .bf.sensor -row 2 -column 7
menu .m.f
.m.f add command -label "Save Config" -command {save_config}
.m.f add command -label "Quit" -command {destroy .}
.m add cascade -label "File" -menu .m.f
.m add cascade -label "View" -menu $dm
.m add cascade -label "Sensor" -menu $sensor_menu
.m add cascade -label "Measure" -menu $tm
. config -menu .m
#pack .m side top -fill x -expand yes
pack .p -side bottom -fill both -expand yes

$c create image 0 0 -image frame_img_scaled -anchor nw

bind .p.f1 "<Configure>" delayed_resize
marker::cursor::add

proc set_gradient {} {
 image create photo p -file $::gradient -format ppm
 setPalette p
 #fillPaletteImg direct gradient_img_scaled
 #FIXME: use fillPaletteImg
 #image create photo gradient_r
 #::imgtools::rotate p -90 gradient_r
 #::imgtools::scale gradient_r 10% gradient_img
 #image delete p gradient_r
 #::imgtools::scale gradient_img 15x[expr {int($::img_height*$::img_scale)}] gradient_img_scaled
}
set_gradient
#set h [expr {int($img_height*$img_scale)}]
#puts "$img_height $img_scale $h"
#incr h $::sash_off
#puts $h
#.p sash place 0 0 $h

proc emissivity_vw {name1 name2 op} {
 update_emissivty $::emissivity
}

if {$play_file eq ""} {
 set errpipe [chan pipe]
 fconfigure [lindex $errpipe 0] -blocking 0
 set vofd [open "|$sensor_drv 2>@ [lindex $errpipe 1]" r+]
 fconfigure $vofd -translation binary
 fconfigure $vofd  -blocking 0
 fileevent $vofd readable process_vstream
 fileevent [lindex $errpipe 0] readable {puts -nonewline [read [lindex $errpipe 0]]}
 load_sensor_config $sensor
 trace add variable emissivity write emissivity_vw
} else {
 start_playback $play_file
}
if {$tcl_platform(os) eq "Windows NT"} {
 if {$debug} {console show}
}
#after 1000 {.p sash place 0 0 400}
#spawn_temp_corr
