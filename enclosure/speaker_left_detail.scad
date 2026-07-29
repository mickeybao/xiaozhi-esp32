/*
  Visual check of the left-side upright speaker installation.
  Presentation only; do not export this combined scene for printing.
*/

use <xiaozhi_enclosure.scad>

$fn = 48;

wall = 2.4;
speaker_installed = [7, 35, 25];
speaker_pos = [-42.1, 1.1625];
battery = [68, 35, 10];
battery_pos = [3.1, 2];

color([0.86, 0.72, 0.10])
    back_shell();

// Actual upright envelope: X=7, Y=35, Z=25.
color([0.82, 0.16, 0.10, 0.82])
    translate([
        speaker_pos[0],
        speaker_pos[1],
        wall + speaker_installed[2]/2
    ])
        cube(speaker_installed, center=true);

// Battery shown transparently to make the 7.7 mm separation visible.
color([0.12, 0.40, 0.82, 0.34])
    translate([
        battery_pos[0],
        battery_pos[1],
        wall + battery[2]/2
    ])
        cube(battery, center=true);
