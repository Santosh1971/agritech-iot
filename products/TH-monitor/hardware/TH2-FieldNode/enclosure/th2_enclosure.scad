// TH2 field node - enclosure concept v0.3 - Agri Sensors and Controls
//
// ONE 40 x 60 mm PCB in the COMMON AWD/TH box. The front and back shells are the AWD parts
// themselves (called from awd_enclosure.scad through `use`), so any change there shows up here.
//
//   TH PCB:    all parts + the 14500 AA holder on the top side, facing the deep back shell.
//              Back side: SHT40, PAIR button and charge LEDs only, facing the front shell:
//              the button and LEDs sit under the AWD membrane / light window (same X/Y).
//   TH-only:   one vent hole in the front shell over the SHT40 (ePTFE membrane + foam pocket),
//              a base with a pole socket (32 mm, sleeves for 25 / 20 mm, hose-clamp slots for
//              bamboo), and a stacked-plate radiation shield around the upright box.
//
// Box frame (as in awd_enclosure.scad): X across the PCB 0..40, Y 0 at the PCB top edge and
// negative downwards, Z 0 at the PCB's back face. World frame: Z up, box upright, gland down.
//
// Views:  -D 'view="assembly"' | "cutaway" | "exploded" | "box_open" | "pcb"

use <../../../../AWD1-paddy/hardware/AWD-FieldNode/enclosure/awd_enclosure.scad>

view = "cutaway";
$fn = 96;

// ---- AWD box constants (must match awd_enclosure.scad) ------------------------------------
wall = 2.5;  front_top = 8.6;  back_bot = -23;
ox0 = -7.3;  ox1 = 43.0;  oy0 = -63.0;  oy1 = 3.0;   // shell outside
fx0 = -14.3; fx1 = 50.0;  fy0 = -70.0;  fy1 = 10.0;  // flange
boss = [16, 8, 17];  boss_z0 = -3;                  // bottom gland boss (strip exit on AWD)
sw_xy = [5, -14];  led_xy = [30.5, -18.5];           // AWD membrane button + light window
sma_y = -25;                                         // AWD SMA hole (TH: magnetic charge socket)

// ---- TH-specific -------------------------------------------------------------------------
vent_xy = [20, -50];  vent_d = 10;                   // SHT40 vent in the front shell
aa_x0 = 21.5;  aa_w = 17.4;  aa_l = 57.5;  aa_h = 15;   // Keystone 2460-class 1xAA holder

// base + pole socket
base_t = 4;  sock_id = 33.8;  sock_len = 45;         // 32 mm (1") PVC pole; sleeves for 25 / 20
// shield
plate_od = 180;  plate_id = 92;  plate_t = 2.5;  plate_drop = 9;
plate_n = 6;  plate_pitch = 17;  post_r = 72;

// box placement in the world: rotate the box frame so +Y -> +Z, then lift it onto the base
box_cx = (fx0 + fx1) / 2;                            // centre of the box across X
box_cz = (back_bot - wall + front_top + wall) / 2;   // centre through the depth (box Z)
base_top = 0;                                        // flange bottom (box Y = fy0) rests here
gx = 20 - box_cx;                                    // gland boss centre in the world (X)
gy = -(boss[1] / 2 + boss_z0 - box_cz);              // gland boss centre in the world (Y)
module to_world() { translate([-box_cx, 0, base_top - fy0]) rotate([90, 0, 0]) translate([0, 0, -box_cz]) children(); }
// after to_world(): box X -> world X, box Y -> world Z, box Z -> world -Y

c_pcb = [0.05, 0.45, 0.20];  c_mod = [0.75, 0.75, 0.78];  c_ant = [0.15, 0.15, 0.15];
c_bat = [0.20, 0.45, 0.85];  c_hold = [0.12, 0.12, 0.12];  c_ic = [0.10, 0.10, 0.10];
c_usb = [0.80, 0.80, 0.82];  c_plate = [1, 1, 1];  c_sht = [0.95, 0.75, 0.10];
c_rod = [0.6, 0.6, 0.62];  c_pole = [0.55, 0.42, 0.25];  c_shell_f = [0.97, 0.97, 0.97];
c_shell_b = [0.85, 0.86, 0.87];

// ---------------------------------------------------------------------------------------------
// TH PCB (box frame). Top side faces -Z (into the back shell); back side faces +Z.
module th_pcb() {
    color(c_pcb) difference() {
        translate([0, -60, 0]) cube([40, 60, 1.6]);
        // milled slot around the SHT40 island (thermal isolation), two bridges
        translate([vent_xy[0], vent_xy[1], -1]) difference() {
            cylinder(d = 13, h = 4);  translate([0, 0, -1]) cylinder(d = 11, h = 6);
            for (a = [0, 180]) rotate(a) translate([0, -1.25, -1]) cube([8, 2.5, 6]);
        }
    }
    // --- top side (Z < 0) ---
    translate([0, 0, 0]) mirror([0, 0, 1]) {
        color(c_mod) translate([1.5, -20, 0]) cube([18, 13, 3.2]);                // ESP32-C3-WROOM-02
        color(c_ant) translate([1.5, -7, 0]) cube([18, 7, 0.8]);                  // antenna to the top edge
        color(c_mod) translate([2, -41, 0]) cube([16, 17, 2.5]);                  // SX1262 (TH2-L)
        color([0.8, 0.6, 0.2]) translate([10, -23, 3]) rotate([0, 90, 0]) cylinder(d = 3, h = 9, $fn = 12);
        color(c_ic) translate([4, -50, 0]) cube([5, 4, 1.7]);                     // CN3058E
        color(c_ic) translate([12, -48, 0]) cube([3, 3, 1.2]);                    // RT9080
        color(c_ic) translate([12, -57, 0]) cube([3, 2.9, 1.1]);                  // USBLC6
        // 1xAA holder + LiFePO4 14500
        translate([aa_x0, -1.25 - aa_l, 0]) {
            color(c_hold) difference() {
                cube([aa_w, aa_l, aa_h]);
                translate([aa_w / 2, 4, 8.5]) rotate([-90, 0, 0]) cylinder(d = 14.8, h = aa_l - 8);
                translate([-1, 6, 8.5]) cube([aa_w + 2, aa_l - 12, 10]);                 // open top
            }
            color(c_bat) translate([aa_w / 2, (aa_l - 50) / 2, 8.5]) rotate([-90, 0, 0]) cylinder(d = 14.2, h = 50);
        }
    }
    // --- back side (Z > 1.6): sensor, button, LEDs only ---
    translate([0, 0, 1.6]) {
        color(c_sht) translate([vent_xy[0] - 0.75, vent_xy[1] - 0.75, 0]) cube([1.5, 1.5, 0.5]);
        color([0.3, 0.3, 0.3]) translate([sw_xy[0] - 3, sw_xy[1] - 3, 0]) cube([6, 6, 1.5]);   // TL3342
        color([1, 0.1, 0.1]) translate([led_xy[0] - 2, led_xy[1] - 0.4, 0]) cube([1.6, 0.8, 0.6]);
        color([0.1, 1, 0.1]) translate([led_xy[0] + 0.4, led_xy[1] - 0.4, 0]) cube([1.6, 0.8, 0.6]);
        // foam ring: seals a small sensor pocket against the inside of the front shell
        color([0.2, 0.2, 0.2]) translate([vent_xy[0], vent_xy[1], 0]) difference() {
            cylinder(d = 16, h = front_top - 1.6);  translate([0, 0, -1]) cylinder(d = 10, h = 20);
        }
    }
}

// TH front shell = AWD front shell + the SHT40 vent (+ ePTFE membrane disc)
module th_front_shell() {
    difference() {
        front_shell();
        translate([vent_xy[0], vent_xy[1], front_top - 1]) cylinder(d = vent_d, h = wall + 2);
    }
    color([0.9, 0.9, 1.0]) translate([vent_xy[0], vent_xy[1], front_top + wall]) cylinder(d = vent_d + 4, h = 0.3);
}

module box(cut = false, explode = 0) {
    color(c_shell_f) translate([0, 0, explode]) th_front_shell();
    if (!cut) color(c_shell_b) translate([0, 0, -explode]) back_shell();   // cutaway: back shell removed
    th_pcb();
}

// ---------------------------------------------------------------------------------------------
// base: socket for the box's gland boss + rod bosses + pole socket below (world frame)
module th_base(cut = false) {
    difference() {
        union() {
            // plate
            translate([0, 0, -base_t]) cylinder(d = 2 * post_r + 16, h = base_t);
            // gland socket sleeve (box gland drops 10 mm into it)
            translate([gx - (boss[0] + 4) / 2, gy - (boss[1] + 4) / 2, -12])
                cube([boss[0] + 4, boss[1] + 4, 12]);
            // pole socket
            translate([0, 0, -base_t - sock_len]) cylinder(d = sock_id + 2 * 3, h = sock_len);
            // ribs
            for (a = [0, 120, 240]) rotate(a) translate([0, -1.5, -base_t - sock_len + 5]) cube([post_r - 8, 3, sock_len - 5]);
        }
        translate([0, 0, -base_t - sock_len - 1]) cylinder(d = sock_id, h = sock_len - 6);          // pole bore
        // hose-clamp slots (bamboo / odd pipes)
        for (a = [45, 225]) rotate(a) translate([sock_id / 2 - 1, -5, -base_t - sock_len + 6]) cube([6, 10, 14]);
        // gland pocket
        translate([gx - (boss[0] + 0.6) / 2, gy - (boss[1] + 0.6) / 2, -13]) cube([boss[0] + 0.6, boss[1] + 0.6, 14]);
        // drain / air holes
        for (a = [60, 180, 300]) rotate(a) translate([post_r * 0.55, 0, -base_t - 1]) cylinder(d = 8, h = base_t + 2);
        if (cut) translate([-300, 0, -300]) cube([600, 300, 600]);
    }
}

module shield(cut = false, explode = 0) {
    difference() {
        union() {
            for (i = [0 : plate_n - 2]) translate([0, 0, 18 + i * (plate_pitch + explode)])
                color(c_plate) rotate_extrude() polygon([[plate_id / 2, 0], [plate_od / 2, -plate_drop],
                    [plate_od / 2, -plate_drop - plate_t], [plate_id / 2, -plate_t]]);
            translate([0, 0, 18 + (plate_n - 1) * (plate_pitch + explode)])                       // solid roof
                color(c_plate) rotate_extrude() polygon([[0, 6], [plate_od / 2, -plate_drop],
                    [plate_od / 2, -plate_drop - plate_t], [0, 6 - plate_t]]);
            for (a = [90, 210, 330]) color(c_rod) rotate(a) translate([post_r, 0, -base_t])
                cylinder(d = 5, h = 18 + (plate_n - 1) * (plate_pitch + explode) + base_t, $fn = 16);
        }
        if (cut) translate([-300, 0, -300]) cube([600, 300, 600]);
    }
}

module pole() { color(c_pole) translate([0, 0, -base_t - sock_len - 60]) cylinder(d = 32, h = sock_len + 54); }

// ---------------------------------------------------------------------------------------------
module assembly(cut = false, explode = 0) {
    translate([0, 0, explode * 2]) to_world() box(cut, explode);
    color([0.92, 0.92, 0.92]) th_base(cut);
    translate([0, 0, explode * 3]) shield(cut, explode);
    pole();
}

if (view == "assembly")  assembly();
if (view == "cutaway")   assembly(cut = true);
if (view == "exploded")  assembly(cut = true, explode = 15);
if (view == "box_open")  { color(c_shell_b) back_shell(); th_pcb(); translate([0, 0, 25]) color(c_shell_f) th_front_shell(); }
if (view == "pcb")       rotate([0, 180, 0]) th_pcb();   // top side up
