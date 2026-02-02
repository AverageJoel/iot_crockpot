// CYD 3.5" Display + Relay Enclosure for Crockpot
// Angled display with overhang, flat back
//
// SIDE VIEW:
//
//         ╱────│
//        ╱     │  <- display at 20° angle
//       ╱      │
//      ╱───────┤  <- barrier + signal slot
//     │ ░░░░░░ │  <- relay section (uniform depth)
//     │  SSR   │
//     │  PSU   │
//     └────────┘
//         ╔═╗
//         ║ ║  <- switch cylinder slides in
//     ════╩═╩════ crockpot
//

/* [Crockpot Switch Cylinder] */
switch_cylinder_od = 22;     // mm - measure this
switch_cylinder_height = 12; // mm
socket_clearance = 0.5;      // mm

/* [CYD 3.5" Display - ESP32-3248S035C actual dimensions] */
cyd_width = 101.5;           // mm - PCB width
cyd_height = 55;             // mm - PCB height
cyd_pcb_thickness = 1.6;     // mm - PCB thickness
cyd_component_height = 10;   // mm - components on back (ESP32 module, etc)
cyd_depth = cyd_pcb_thickness + cyd_component_height;  // total depth
cyd_screen_width = 73.5;     // mm - active display area
cyd_screen_height = 49;      // mm - active display area
cyd_screen_offset_x = 5;     // mm - screen offset from left edge
cyd_screen_offset_y = 3;     // mm - screen offset from bottom edge
cyd_bezel_thickness = 4;     // mm - display bezel/glass on front
// Mounting holes (estimated - 4 corners)
cyd_hole_dia = 3;            // mm - M3 mounting holes
cyd_hole_inset = 3;          // mm - distance from edge to hole center

/* [Display Angle] */
display_angle = 20;          // degrees - tilt toward user

/* [Relay Section] */
// Tall components (SSR ~20mm, HLK-PM01 ~20mm) go at BOTTOM (low Y)
max_component_height = 22;   // mm - tallest component (HLK-PM01 or SSR)
relay_depth = 20;            // mm - reduced depth for relay section

/* [Isolation] */
isolation_gap = 6;           // mm
barrier_thickness = 2;       // mm
signal_slot_width = 12;      // mm
signal_slot_height = 5;      // mm

/* [Enclosure] */
wall_thickness = 2.5;        // mm

// Derived dimensions
enclosure_width = cyd_width + wall_thickness * 2 + 2;
enclosure_height = cyd_height + wall_thickness * 2 + 2;
socket_id = switch_cylinder_od + socket_clearance * 2;

// Overhang calculated from angle so front face and display are PARALLEL
display_overhang = enclosure_height * tan(display_angle);

// Calculate geometry - display depth is part of the angled section, not added to back
back_depth = relay_depth + wall_thickness + isolation_gap + barrier_thickness;

$fn = 64;

module assembly() {
    color("DarkSlateGray") enclosure_body();

    barrier_z = cyd_depth + cyd_bezel_thickness + wall_thickness + 3;

    // CYD dummy (angled - bottom near barrier, top forward)
    %translate([wall_thickness + 1, enclosure_height - wall_thickness - 1, -display_overhang + wall_thickness])
        rotate([-display_angle, 0, 0])
            translate([0, -cyd_height, 0])
                cyd_dummy();

    // Relay components in relay section at TOP (high Y - near socket)
    %translate([wall_thickness + 5, enclosure_height - wall_thickness - 25, barrier_z + barrier_thickness + isolation_gap + 2])
        relay_components_dummy();

    // Switch cylinder dummy at TOP (high Y - slides in from crockpot)
    %translate([enclosure_width/2, enclosure_height - wall_thickness - socket_id/2 - 3, back_depth + 2])
        color("Brown", 0.3)
            cylinder(d=switch_cylinder_od, h=switch_cylinder_height);
}

module enclosure_body() {
    // front_bottom_z = 0 (reference point)
    // front_top_z = -display_overhang (overhangs forward)
    // back is at uniform depth: back_depth

    difference() {
        // Main body with angled front, flat back
        hull() {
            // Front bottom edge (at z=0)
            translate([0, 0, 0])
                cube([enclosure_width, 0.1, wall_thickness]);

            // Front top edge (overhangs forward, negative z)
            translate([0, enclosure_height, -display_overhang])
                cube([enclosure_width, 0.1, wall_thickness]);

            // Back bottom edge
            translate([0, 0, back_depth])
                cube([enclosure_width, 0.1, wall_thickness]);

            // Back top edge (same depth - flat back)
            translate([0, enclosure_height, back_depth])
                cube([enclosure_width, 0.1, wall_thickness]);
        }

        // ========== DISPLAY POCKET (angled) ==========

        // Display bottom is near barrier, top overhangs forward
        // Pocket includes space for bezel in front and components behind
        translate([wall_thickness + 1, enclosure_height - wall_thickness - 1, -display_overhang + wall_thickness])
            rotate([-display_angle, 0, 0])
                translate([0, -cyd_height, -cyd_bezel_thickness - 1])
                    cube([cyd_width, cyd_height, cyd_depth + cyd_bezel_thickness + 2]);

        // Screen cutout (angled, through front face)
        translate([
            wall_thickness + 1 + cyd_screen_offset_x,
            enclosure_height - wall_thickness - 1 - cyd_height + cyd_screen_offset_y,
            -display_overhang - 5
        ])
            rotate([-display_angle, 0, 0])
                translate([0, 0, 0])
                    cube([cyd_screen_width, cyd_screen_height, wall_thickness + display_overhang + 10]);

        // USB access (left side, near bottom of display which is now at back)
        translate([-0.1, wall_thickness + 15, wall_thickness + cyd_depth])
            cube([wall_thickness + 0.2, 14, 10]);

        // ========== RELAY CAVITY (uniform depth) ==========

        barrier_z = cyd_depth + cyd_bezel_thickness + wall_thickness + 3;  // after display section

        translate([wall_thickness, wall_thickness, barrier_z + barrier_thickness + isolation_gap])
            cube([
                enclosure_width - wall_thickness * 2,
                enclosure_height - wall_thickness * 2,
                relay_depth
            ]);

        // ========== SIGNAL SLOT ==========

        // At BOTTOM of barrier (low Y - away from mains at top)
        translate([
            wall_thickness + 8,
            wall_thickness + 8,
            barrier_z - 0.1
        ])
            cube([signal_slot_width, signal_slot_height, barrier_thickness + isolation_gap + 0.2]);

        // ========== SOCKET FOR SWITCH CYLINDER ==========

        // Open hole at back, at TOP (high Y - where mains/high power is)
        translate([enclosure_width/2, enclosure_height - wall_thickness - socket_id/2 - 3, barrier_z + barrier_thickness])
            cylinder(d=socket_id, h=back_depth);
    }

    // Isolation barrier (solid floor between display and relay)
    barrier_z = cyd_depth + cyd_bezel_thickness + wall_thickness + 3;
    difference() {
        translate([wall_thickness, wall_thickness, barrier_z])
            cube([enclosure_width - wall_thickness*2, enclosure_height - wall_thickness*2, barrier_thickness]);

        // Signal slot through barrier (at BOTTOM, low Y)
        translate([
            wall_thickness + 8,
            wall_thickness + 8,
            barrier_z - 0.1
        ])
            cube([signal_slot_width, signal_slot_height, barrier_thickness + 0.2]);
    }

    // Socket rim at TOP (high Y - where mains is)
    translate([enclosure_width/2, enclosure_height - wall_thickness - socket_id/2 - 3, back_depth - 3])
        difference() {
            cylinder(d=socket_id + 5, h=3);
            translate([0, 0, -0.1])
                cylinder(d=socket_id, h=3.2);
        }
}

module cyd_dummy() {
    // Detailed ESP32-3248S035C model

    // PCB (yellow, characteristic of CYD)
    color("Gold", 0.8)
        difference() {
            cube([cyd_width, cyd_height, cyd_pcb_thickness]);
            // Mounting holes
            for (x = [cyd_hole_inset, cyd_width - cyd_hole_inset]) {
                for (y = [cyd_hole_inset, cyd_height - cyd_hole_inset]) {
                    translate([x, y, -0.1])
                        cylinder(d=cyd_hole_dia, h=cyd_pcb_thickness + 0.2, $fn=16);
                }
            }
        }

    // Display bezel/glass (front, raised)
    color("DimGray", 0.9)
        translate([2, 1, -cyd_bezel_thickness])
            cube([cyd_width - 4, cyd_height - 2, cyd_bezel_thickness]);

    // Active display area (black screen)
    color("Black", 0.95)
        translate([cyd_screen_offset_x, cyd_screen_offset_y, -cyd_bezel_thickness - 0.1])
            cube([cyd_screen_width, cyd_screen_height, 0.2]);

    // Components on back of PCB
    // ESP32-WROOM module
    color("DarkGray", 0.7)
        translate([cyd_width - 30, 10, cyd_pcb_thickness])
            cube([25, 18, 3]);

    // SD card slot
    color("Silver", 0.8)
        translate([5, cyd_height - 15, cyd_pcb_thickness])
            cube([15, 14, 2]);

    // USB Micro connector (left side)
    color("Silver", 0.8)
        translate([-2, 20, cyd_pcb_thickness])
            cube([7, 8, 3]);

    // Misc components (capacitors, etc)
    color("DarkSlateGray", 0.6)
        translate([40, 25, cyd_pcb_thickness])
            cube([20, 15, 2]);
}

module relay_components_dummy() {
    // Components laid out to fit in bottom of wedge
    // SSR (small PCB mount, ~20mm tall when vertical)
    color("Navy", 0.5) cube([20, 20, 8]);

    // HLK-PM01 (~20mm tall)
    translate([25, 0, 0])
        color("DarkGreen", 0.5) cube([34, 20, 15]);
}

assembly();

// ============================================================
// DIMENSIONS
// ============================================================
//
// Width:  ~106 mm
// Height: ~66 mm
// Depth (back): ~41 mm (uniform, flat back)
// Display overhang: 15 mm forward at top
//
// Adjust display_angle (15-25°) for viewing angle
// Adjust display_overhang to extend/shorten the angled face
// Adjust relay_depth if you need more/less room for components
//
// ============================================================
// LAYOUT
// ============================================================
//
// FRONT: Angled display (CYD 3.5")
// BARRIER: Isolation wall + signal slot (low voltage only)
// BACK: Relay section - SSR, HLK-PM01, wiring
// SOCKET: Switch cylinder from crockpot slides into back
//
// ============================================================
