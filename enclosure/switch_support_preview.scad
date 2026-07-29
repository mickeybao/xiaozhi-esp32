// Detail view of the side-switch support. Presentation model only.
use <xiaozhi_enclosure.scad>

$fn = 36;

// A small section of the +X enclosure wall around the 7.4 x 7.4 opening.
color([0.78, 0.82, 0.88, 0.85])
    difference() {
        translate([43.8, 22, 7.5])
            cube([2.4, 20, 15], center=true);
        translate([43.8, 22, 7.5])
            cube([4.4, 4.4, 4.4], center=true);
    }

color([0.78, 0.82, 0.88, 0.9])
    switch_cradle();

// Reference clamped switch body. The requested support geometry defines a
// 7 mm body depth; actuator and pins account for the remaining overall length.
color([0.12, 0.12, 0.14, 0.72])
    translate([39.1, 22, 5.9])
        cube([7, 7, 7], center=true);

// 4 x 4 x 5 mm blue actuator; about 2.6 mm remains outside a 2.4 mm wall.
color([0.10, 0.32, 0.88])
    translate([45.1, 22, 5.9])
        cube([5, 4, 4], center=true);

// Four-millimetre pin/wire-side clearance reference.
color([0.78, 0.60, 0.12])
    for (dy = [-3, 3])
        translate([32.6, 22+dy, 5.9])
            rotate([0, 90, 0])
                cylinder(d=0.8, h=4);
