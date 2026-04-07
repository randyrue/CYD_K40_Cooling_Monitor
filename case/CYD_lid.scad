// Lid for the box
$fn=20;

include <constants.scad>
lid_layer_thickness = 2;
lid_inset = 0.2;

// top part full width
minkowski() {
    cube([8+board_x_length+(wall_thickness*2), 8+board_y_width+(wall_thickness*2), lid_layer_thickness], center = true);
    cylinder(r=1,h=1);
    }
translate([0, 0, lid_layer_thickness])
difference() {
// bit that fits into the hole in the box
    cube([8+board_x_length - lid_inset, 8+board_y_width - lid_inset, lid_layer_thickness+2], center = true);
    translate([0, 0, 0.1])
    cube([8+board_x_length - lid_inset - lid_layer_thickness, 8+board_y_width - lid_inset - lid_layer_thickness, lid_layer_thickness+2], center = true);

}

cube([100,64,1],center=true);