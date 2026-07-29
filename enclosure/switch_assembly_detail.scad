// Exploded detail of the switch loading notch and front-shell locking tongue.
use <xiaozhi_enclosure.scad>

$fn = 40;

module detail_crop(z_height=18) {
    translate([37, 22, z_height/2])
        cube([24, 24, z_height], center=true);
}

// Rear shell section with the upward-open loading notch and three supports.
color([0.76, 0.82, 0.88])
    intersection() {
        back_shell();
        detail_crop(16);
    }

// Reference switch in its installed location.
color([0.12, 0.12, 0.14, 0.80])
    translate([39.1, 22, 5.9])
        cube([7, 7, 7], center=true);
color([0.10, 0.32, 0.88])
    translate([45.1, 22, 5.9])
        cube([5, 4, 4], center=true);

// Front shell shown 10 mm above its assembled position. Its tongue points
// downward and enters the rear-shell loading notch when the shells close.
color([0.92, 0.70, 0.20, 0.82])
    intersection() {
        translate([0, 0, 20+21+10])
            rotate([180, 0, 0])
                front_shell();
        translate([37, 22, 22])
            cube([24, 24, 30], center=true);
    }
