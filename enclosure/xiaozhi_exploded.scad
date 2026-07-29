/*
  XiaoZhi enclosure installation exploded view.
  This is a presentation/assembly model, not a printable combined part.
*/

use <xiaozhi_enclosure.scad>

$fn = 48;

pcb = [82, 46, 1.6];
display = [37.10, 58.80, 1.10];
display_pcb = [35.00, 48.20, 1.60];
display_total_depth = 9.70;
display_pos = [3.516, 0];
pcb_offset = [3, 3];
mic_pos = [32.899, 15.504];
battery = [68, 35, 10];
battery_pos = [3.1, 2];
// Upright at the left wall: 7 mm X, 35 mm Y, 25 mm Z.
speaker = [7, 35, 25];
speaker_pos = [-42.1, 1.1625];
case_bosses = [
    [-43.0, -29.5], [-43.0, 29.5],
    [ 43.0, -29.5], [ 43.0, 29.5]
];

// Exploded Z levels
z_back = 0;
z_battery = 31;
z_pcb = 57;
z_display = 80;
z_front_split = 122;

module rounded_component(s, r=1.5) {
    linear_extrude(height=s[2])
        offset(r=r)
            square([s[0]-2*r, s[1]-2*r], center=true);
}

module pcb_model() {
    difference() {
        color([0.05, 0.42, 0.20])
            cube(pcb, center=true);
        for (p = [
            [-37.425, 17.263],
            [-37.425, -20.938],
            [37.613, -0.815]
        ])
            translate([p[0], p[1], -pcb[2]])
                cylinder(d=3, h=pcb[2]*3);
    }
    // Simplified main-board components for orientation.
    color([0.12, 0.12, 0.14])
        translate([0.5, -20, pcb[2]/2])
            cube([10, 5, 3], center=true);
}

module display_model() {
    // 37.10 x 58.80 mm front glass/display.
    color([0.05, 0.10, 0.14])
        rounded_component(display, 1.2);
    color([0.08, 0.40, 0.72])
        translate([0, 0, display[2]])
            linear_extrude(height=0.25)
                square([34, 55.5], center=true);

    // 35 x 48.20 mm controller PCB on the rear of the screen.
    color([0.05, 0.35, 0.22])
        translate([0, 1.8, display_total_depth-display_pcb[2]])
            rounded_component(display_pcb, 1.5);

    // Type-C belongs to the display and faces out of its opposite edge.
    color([0.72, 0.74, 0.78])
        translate([0, display[1]/2+2.8, display_total_depth-3.2])
            cube([13, 5.6, 3.2], center=true);
}

module microphone_model() {
    color([0.68, 0.70, 0.73])
        cylinder(r=7, h=3);
    color([0.12, 0.12, 0.12])
        translate([0, 0, 3])
            cylinder(d=2.5, h=0.3);
}

module speaker_model() {
    color([0.12, 0.12, 0.14])
        rounded_component(speaker, 1.5);
    color([0.33, 0.33, 0.36])
        translate([-speaker[0]/2, 0, speaker[2]/2])
            rotate([0, -90, 0])
                scale([1.35, 1, 0.88])
                    cylinder(d=18, h=1);
}

module battery_model() {
    color([0.16, 0.38, 0.72])
        rounded_component(battery, 3);
    color([0.92, 0.72, 0.12])
        translate([0, 0, battery[2]])
            linear_extrude(height=0.25)
                square([24, 12], center=true);
}

module switch_model() {
    color([0.12, 0.12, 0.13])
        cube([7, 7, 7], center=true);
    color([0.10, 0.32, 0.88])
        translate([6, 0, 0])
            cube([5, 4, 4], center=true);
    color([0.78, 0.62, 0.16])
        for (dy = [-3, 3])
            translate([-7.5, dy, 0])
                rotate([0, 90, 0])
                    cylinder(d=0.8, h=4);
}

module screw_model(length=25) {
    color([0.48, 0.50, 0.54]) {
        cylinder(d=2.5, h=length);
        translate([0, 0, -1.8])
            cylinder(d1=5.5, d2=4.2, h=1.8);
    }
}

module step_marker(n, z) {
    color([0.95, 0.55, 0.08]) {
        translate([-53, 0, z])
            cylinder(d=11, h=1.3);
        translate([-53, 0, z+1.3])
            linear_extrude(height=0.5)
                text(str(n), size=6, halign="center", valign="center",
                     font="Arial:style=Bold");
    }
}

// 1. Rear enclosure
color([0.78, 0.82, 0.88, 0.88])
    translate([0, 0, z_back])
        back_shell();
step_marker(1, z_back+2);

// 2. Battery, speaker and side switch
translate([battery_pos[0], battery_pos[1], z_battery])
    battery_model();
translate([speaker_pos[0], speaker_pos[1], z_battery])
    speaker_model();
translate([42.1, 22, z_battery+6])
    switch_model();
step_marker(2, z_battery+2);

// 3. PCB and microphone
translate([pcb_offset[0], pcb_offset[1], z_pcb])
    pcb_model();
translate([mic_pos[0], mic_pos[1], z_pcb+5])
    microphone_model();
step_marker(3, z_pcb);

// 4. Display module
translate([display_pos[0], display_pos[1], z_display])
    display_model();
step_marker(4, z_display);

// 5. Front enclosure, face upwards
color([0.88, 0.90, 0.94, 0.78])
    translate([0, 0, z_front_split])
        rotate([180, 0, 0])
            front_shell();
step_marker(5, z_front_split-10);

// Four assembly screws shown below the rear shell.
for (p = case_bosses)
    translate([p[0], p[1], -31])
        screw_model(25);

// Slim direction arrow indicating assembly order.
color([0.95, 0.55, 0.08])
    translate([54, 0, 2]) {
        cylinder(d=1.8, h=98);
        translate([0, 0, 98])
            cylinder(d1=7, d2=0, h=9);
    }
