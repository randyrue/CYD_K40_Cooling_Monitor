// v4
$fn=40;


include <constants.scad>    // things shared with the lid
box_height = 26;
display_width = 44;
display_length = 59;
display_x_offset = 3;  // from the antenna end
display_y_offset = 4;
display_thickness = 3;  // from the main board
overlap = 0.01;
screw_mount_inset = 6;
screw_mount_height = 4;
screw_mount_radius = 3;
screw_hole_radius = 1.6;
board_thickness = 1.5;
usb_width = 12;
usb_y_offset = (screw_mount_height / 2) - 1.5;
opening_depth = 4;  // opening hole depth
board_top_z = screw_mount_height + board_thickness + wall_thickness; // opening hole z

include <roundedcube.scad>
//translate([-1,-1,0]) cube([100,64,10]);

difference() {
    minkowski() {
        cube([board_x_length+(wall_thickness*2+8), board_y_width+(wall_thickness*2+8), box_height]);
        cylinder(r=1,h=1);
        }
    
    // box main cavity
    translate([wall_thickness, wall_thickness, wall_thickness])
    cube([board_x_length+8, board_y_width+8, box_height]);
    
    // diplay opening
    translate([4+ (board_x_length / 2), 4+board_y_width / 2, -overlap])
    translate([wall_thickness+display_x_offset, wall_thickness, 0])
    cube([display_length, display_width, box_height], center=true);
    
    // USB-C port
    translate([-overlap, (board_y_width / 2) + wall_thickness, (box_height / 2) + usb_y_offset])
    roundedcube([10, usb_width, 6], radius = 2, center=true);
    
    // fingernail slot on end to help take off the lid
    finger_hole_radius=5;
    translate([board_x_length+finger_hole_radius+8, (board_y_width / 2) + wall_thickness, box_height+3.5])
    rotate([0,-45,0])
    cylinder(h=10, r=finger_hole_radius, center=true);

    // UART opening
    uart_width = 10;
    uart_height = 5;
    uart_start_x = 30.5 + wall_thickness;  // left edge
    //translate([uart_start_x, -overlap, board_top_z])
    //cube([uart_width, uart_height, opening_depth]);
    
    // SD Card opening
    sd_width = 16;
    sd_height = 2;
    sd_start_x = 43 + wall_thickness;
    //translate([sd_start_x, -overlap, board_top_z])
    //cube([sd_width, opening_depth, sd_height]);
    
    // SPI socket opening
    spi_width = 8;
    spi_height = 5;
    spi_start_x = 47.5 + wall_thickness;
    translate([spi_start_x, board_y_width + overlap, board_top_z])
    cube([spi_width, spi_height, opening_depth]);
    
    // IO35 socket opening
    io35_width = 8;
    io35_height = 5;
    io35_start_x = 64 + wall_thickness;
    translate([io35_start_x, board_y_width + overlap, board_top_z])
    cube([io35_width, io35_height, opening_depth]);
}

// screw mounts
corner_screws();

module corner_screws() {
    // right edge near USB socket
    translate([4+screw_mount_inset, 
        screw_mount_inset+4, 
        wall_thickness])
    screw_mount();
    
    // left edge near USB socket
    translate([4+screw_mount_inset, 
        board_y_width - screw_mount_inset + (wall_thickness*2)+4, 
        wall_thickness])
    screw_mount();
    
    // left far end from USB
    translate([4+board_x_length - screw_mount_inset + (wall_thickness*2), 
        board_y_width - screw_mount_inset + (wall_thickness*2)+4, 
        wall_thickness])
    screw_mount();
    
    // right far end from USB
    translate([4+board_x_length - screw_mount_inset + (wall_thickness*2), 
        screw_mount_inset+4, 
        wall_thickness])
    screw_mount();
}

module screw_mount() {
    //translate([6, 6, wall_thickness - overlap])
    difference() {
        cylinder(screw_mount_height, screw_mount_radius, screw_mount_radius); // h, r1, r2
        
        translate([0,0,wall_thickness])
        cylinder(screw_mount_height, screw_hole_radius, screw_hole_radius); // h, r1, r2
    }

}