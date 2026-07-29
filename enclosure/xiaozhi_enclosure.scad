/*
  XiaoZhi custom enclosure, dimensions in millimetres.

  Export:
    part = "front";  -> front shell STL
    part = "back";   -> back shell STL
    part = "preview";-> assembled enclosure with component envelopes

  Coordinate system on the PCB: X = 82 mm direction, Y = 46 mm direction.
  Compact enclosure: the PCB is offset 3 mm upward so the 58.80 mm display can
  fit inside a 65 mm-high front face.
*/

$fn = 48;
part = "preview";             // "front", "back", or "preview"
show_components = true;

// Given dimensions
pcb = [82, 46, 1.6];
// The compact layout is shifted 3 mm right so increasing width from 90 to
// 96 mm adds the full 6 mm on the left side.
pcb_offset = [3, 3];
pcb_holes_given = [
    [-37.425,  17.263],
    [-37.425, -20.938],
    [ 37.613,  -0.815]
];
pcb_holes = [
    for (p = pcb_holes_given)
        [p[0]+pcb_offset[0], p[1]+pcb_offset[1]]
];
pcb_hole_r = 1.5;
// Microphone coordinate on the PCB and its X-axis-mirrored coordinate on the
// front shell. The shell is viewed from outside and is flipped during assembly,
// so its local Y must be the opposite of the PCB/top-view Y.
pcb_mic_given = [29.899, 12.504];
pcb_mic_pos = [
    pcb_mic_given[0]+pcb_offset[0],
    pcb_mic_given[1]+pcb_offset[1]
];
front_mic_pos = [pcb_mic_pos[0], -pcb_mic_pos[1]];
mic_r = 7;
display = [37.10, 58.80];
// Front-view placement: PCB mounting pattern has two holes on the left and
// one on the right. The Y centre balances the requested nominal 10 mm lower
// and 4 mm upper PCB overhangs while preserving the real 58.80 mm screen.
display_pos = [3.516, 0];
display_to_pcb = 18;
typec = [13, 7];
typec_pos = [
    display_pos[0],
    display_pos[1] + display[1]/2 + 8
];
// The Type-C connector belongs to the display module. Its side-wall opening
// sits in the front shell at the display-controller depth, not at the PCB split.
display_module_depth = 9.70;
battery = [68, 35, 10];
// Back-side installation orientation: rotate the 68 x 35 battery by 90°.
battery_installed = battery;
battery_pos = [3.1, 2];
speaker = [25, 35, 7];
// Left-side standing envelope: 7 mm inward from the -X wall, 35 mm along Y,
// and 25 mm along enclosure depth.
speaker_installed = [speaker[2], speaker[1], speaker[0]];
speaker_pos = [-42.1, 1.1625];
switch_body = [7, 7, 14];
switch_plunger = [4, 4, 5];
switch_travel = 1.5;
switch_y = 22;
switch_opening = [4.4, 4.4]; // Y x Z, 4 mm plunger + 0.20 mm per edge
switch_side_block = [7, 3, 7]; // X x Y x Z, rectangles 1 and 2
switch_rear_block = [4, 2, 7]; // X x Y x Z, narrower and thicker rectangle 3
switch_body_gap = 7.4;
switch_stop_distance = 7;
switch_service_notch_y = 7.8;
switch_tongue_fit = 0.25;

// Print/tolerance parameters
wall = 2.4;
fit = 0.25;                  // general radial/edge clearance
// Height expanded so the Type-C centre can sit 8 mm below the display.
outer = [96, 65];
corner_r = 5;
front_depth = 21;
back_depth = 20;
// 7.4 mm actual opening height: centre at 7.7 mm leaves exactly 4.0 mm
// between the opening's upper edge and the enclosure's outer top face.
typec_front_z = 7.70;
// The 7 mm-high switch body stands directly on the 2.4 mm inner floor.
switch_z = wall + switch_side_block[2]/2;
lip_h = 2.0;
lip_t = 1.2;
screen_clearance = 0.20;     // each edge; resulting aperture 37.50 x 59.20
typec_clearance = 0.20;      // each edge; resulting aperture 13.40 x 7.40
vent_d = 2.2;
pcb_boss_od = 5.4;
boss_od = 6.0;
case_screw_d = 2.8;          // M2.5 clearance from the back
pilot_d = 2.2;               // self-tapping pilot in front bosses
case_bosses = [
    [-43.0, -29.5], [-43.0, 29.5],
    [ 43.0, -29.5], [ 43.0, 29.5]
];

module rounded_xy_box(s, h, r, z0=0) {
    translate([0, 0, z0])
        linear_extrude(height=h)
            offset(r=r)
                square([s[0]-2*r, s[1]-2*r], center=true);
}

module front_shell() {
    difference() {
        union() {
            difference() {
                rounded_xy_box(outer, front_depth, corner_r);
                // Open rear, retain the front face.
                rounded_xy_box(
                    [outer[0]-2*wall, outer[1]-2*wall],
                    front_depth-wall+0.1,
                    max(corner_r-wall, 0.8),
                    wall
                );
            }

            // Screw receivers positioned outside the PCB outline.
            for (p = case_bosses)
                translate([p[0], p[1], wall])
                    cylinder(d=boss_od, h=front_depth-wall);

            speaker_cradle_front();

            // Rear-facing locating lip.
            difference() {
                translate([0, 0, front_depth-lip_h])
                    rounded_xy_box(
                        [outer[0]-2*wall-2*fit,
                         outer[1]-2*wall-2*fit],
                        lip_h, max(corner_r-wall-fit, 0.8)
                    );
                translate([0, 0, front_depth-lip_h-0.1])
                    rounded_xy_box(
                        [outer[0]-2*wall-2*fit-2*lip_t,
                         outer[1]-2*wall-2*fit-2*lip_t],
                        lip_h+0.2,
                        max(corner_r-wall-fit-lip_t, 0.6)
                    );
            }

            /*
              Removable switch-service tongue. In the assembled orientation
              this projects downward from the front shell into the open notch
              in the back shell. The tongue is slightly recessed from the
              outside surface and has 0.25 mm clearance on each Y edge.
            */
            switch_notch_lower = switch_z + switch_opening[1]/2;
            switch_tongue_h =
                back_depth - switch_notch_lower - switch_tongue_fit;
            translate([
                outer[0]/2-wall/2,
                -switch_y,
                front_depth-0.20 + switch_tongue_h/2
            ])
                cube([
                    wall-2*switch_tongue_fit,
                    switch_service_notch_y-2*switch_tongue_fit,
                    switch_tongue_h+0.40
                ], center=true);
        }

        // Exact component aperture plus a practical FDM clearance.
        translate([display_pos[0], display_pos[1], wall/2])
            cube([display[0]+2*screen_clearance,
                  display[1]+2*screen_clearance,
                  wall+0.2], center=true);

        // Seven equally spaced acoustic holes inside the microphone envelope.
        translate([front_mic_pos[0], front_mic_pos[1], -0.1])
            cylinder(d=vent_d, h=wall+0.2);
        for (a = [0:60:300])
            translate([front_mic_pos[0]+4.1*cos(a),
                       front_mic_pos[1]+4.1*sin(a), -0.1])
                cylinder(d=vent_d, h=wall+0.2);

        // Front-boss self-tapping pilots.
        for (p = case_bosses)
            translate([p[0], p[1], front_depth-9])
                cylinder(d=pilot_d, h=9.2);

        // Type-C opening on the opposite (+Y) side of the screen module.
        // Centre the cutter on the physical top-wall thickness.
        translate([
            typec_pos[0],
            outer[1]/2 - wall/2,
            typec_front_z
        ])
            cube([typec[0]+2*typec_clearance, wall+2,
                  typec[1]+2*typec_clearance], center=true);

        speaker_grille_front();
    }
}

module pcb_supports() {
    // PCB underside is 1.6 mm below the split plane; supports rise from back.
    support_top = back_depth - pcb[2];
    for (p = pcb_holes) {
        translate([p[0], p[1], wall])
            difference() {
                cylinder(d=pcb_boss_od, h=support_top-wall);
                cylinder(d=2.7, h=support_top-wall+0.1);
            }
        // Short registration peg; use M2.5 hardware instead if preferred.
        translate([p[0], p[1], support_top])
            cylinder(d=2.7, h=pcb[2]+0.7);
    }
}

module switch_cradle() {
    /*
      Three upright printed blocks on the rear-shell floor:
        rectangles 1 and 2: 7 x 3 mm footprint, 7 mm high;
        rectangle 3:        4 x 2 mm footprint, 7 mm high.
      There is no printed top or bottom bridge. The enclosure floor supports
      the switch and the side wall carries the button-push load.
    */
    inner_face_x = outer[0]/2 - wall;
    side_center_x = inner_face_x - switch_side_block[0]/2 + 0.20;

    // Rectangles 1 and 2 clamp the 7 mm body from the Y sides. A 7.4 mm
    // clear gap supplies 0.20 mm printing clearance on each side.
    for (dy = [-1, 1])
        translate([
            side_center_x,
            switch_y + dy*(switch_body_gap+switch_side_block[1])/2,
            wall + switch_side_block[2]/2
        ])
            cube(switch_side_block, center=true);

    // Rectangle 3 begins 7 mm inboard from the enclosure inner face. Its
    // 2 mm pin-gap width exceeds the 1.2 mm printing minimum; its X thickness
    // is increased to 4 mm for better resistance to button-push loads.
    translate([
        inner_face_x - switch_stop_distance - switch_rear_block[0]/2,
        switch_y,
        wall + switch_rear_block[2]/2
    ])
        cube(switch_rear_block, center=true);
}

module speaker_cradle_back() {
    // The 7 x 35 x 25 mm speaker stays upright against the LEFT wall:
    // 7 mm in X, 35 mm in Y and 25 mm in Z.  The two left PCB posts
    // double as supports; short printable tabs extend from the posts
    // toward the speaker and leave 0.25 mm assembly clearance.
    speaker_inner_x = speaker_pos[0] + speaker_installed[0]/2;
    // Overlap the cylindrical post by 0.40 mm so the tab is a single,
    // printable solid rather than merely tangent to the post.
    post_left_x = pcb_holes[0][0] - pcb_boss_od/2 + 0.40;
    tab_left_x = speaker_inner_x + fit;
    tab_w = post_left_x - tab_left_x;
    tab_center_x = (tab_left_x + post_left_x)/2;

    for (i = [0, 1])
        translate([
            tab_center_x,
            pcb_holes[i][1],
            (wall+back_depth)/2
        ])
            cube([tab_w, pcb_boss_od, back_depth-wall], center=true);
}

module speaker_cradle_front() {
    // The rear-post tabs locate the upright speaker; the closed front shell
    // limits its axial movement, so no obstructing top bridge is required.
}

module speaker_grille_back() {
    // Lower rows through the LEFT side wall.
    for (y = [-12:6:12])
        for (z = [5, 10, 15])
            translate([
                -outer[0]/2+wall+0.5,
                speaker_pos[1]+y,
                z
            ])
                rotate([0, -90, 0])
                    cylinder(d=vent_d, h=wall+2);
}

module speaker_grille_front() {
    // Upper rows continue through the LEFT wall of the front shell.
    for (y = [-12:6:12])
        for (z = [14, 19])
            translate([
                -outer[0]/2+wall+0.5,
                -speaker_pos[1]+y,
                z
            ])
                rotate([0, -90, 0])
                    cylinder(d=vent_d, h=wall+2);
}

module back_shell() {
    difference() {
        union() {
            difference() {
                rounded_xy_box(outer, back_depth, corner_r);
                rounded_xy_box(
                    [outer[0]-2*wall, outer[1]-2*wall],
                    back_depth-wall+0.1,
                    max(corner_r-wall, 0.8),
                    wall
                );
            }
            pcb_supports();
            switch_cradle();
            speaker_cradle_back();
        }

        speaker_grille_back();

        // Rear screw access holes.
        for (p = case_bosses)
            translate([p[0], p[1], -0.1])
                cylinder(d=case_screw_d, h=back_depth+0.2);

        // Only the 4 x 4 mm blue plunger passes through the side wall. The
        // 7 x 7 mm body stops against the inner wall and carries push loads.
        // The 4.4 mm hole leaves ample printable material in the 20 mm back.
        translate([outer[0]/2-wall/2, switch_y, switch_z])
            cube([wall+2, switch_opening[0],
                  switch_opening[1]], center=true);

        // Open the wall above the button aperture so the switch can be lowered
        // into the three-block holder before the front shell is installed.
        switch_notch_lower = switch_z + switch_opening[1]/2;
        translate([
            outer[0]/2-wall/2,
            switch_y,
            (switch_notch_lower+back_depth+1)/2
        ])
            cube([
                wall+2,
                switch_service_notch_y,
                back_depth+1-switch_notch_lower
            ], center=true);

    }
}

module component_preview() {
    // Transparent envelopes are intentionally non-printing reference geometry.
    color([0.0, 0.45, 0.1, 0.65])
        translate([
            pcb_offset[0], pcb_offset[1],
            back_depth-pcb[2]/2
        ])
            cube(pcb, center=true);

    // Display glass and the smaller 35 x 48.2 mm rear controller envelope.
    color([0.1, 0.4, 0.9, 0.5])
        translate([
            display_pos[0], display_pos[1],
            front_depth+back_depth-wall-1.1/2
        ])
            cube([display[0], display[1], 1.1], center=true);
    color([0.05, 0.3, 0.18, 0.5])
        translate([
            display_pos[0], display_pos[1],
            front_depth+back_depth-wall-1.1-(display_module_depth-1.1)/2
        ])
            cube([35, 48.2, display_module_depth-1.1], center=true);

    color([0.95, 0.75, 0.15, 0.6])
        translate([battery_pos[0], battery_pos[1], wall+battery[2]/2])
            cube(battery_installed, center=true);

    color([0.85, 0.2, 0.15, 0.6])
        translate([
            speaker_pos[0], speaker_pos[1],
            wall+speaker_installed[2]/2
        ])
            cube(speaker_installed, center=true);

    color([0.65, 0.65, 0.65, 0.7])
        translate([pcb_mic_pos[0], pcb_mic_pos[1], back_depth+2])
            cylinder(r=mic_r, h=3);
}

if (part == "front") {
    front_shell();
} else if (part == "back") {
    back_shell();
} else {
    color([0.92, 0.92, 0.94, 0.75])
        back_shell();
    color([0.80, 0.84, 0.90, 0.55])
        translate([0, 0, back_depth+front_depth])
            rotate([180, 0, 0])
                front_shell();
    if (show_components)
        component_preview();
}
