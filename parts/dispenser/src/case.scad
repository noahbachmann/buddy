// Case: Dev's box with every change of this module, feet included. One piece: the gate gap's chain (floor -> divider
// -> right gate) must not have a joint in it.
// Prints on its back (back wall's outer face down, -Y up): the divider slot and the gate gap then lie in the XY plane
// (most precise, no bridge over the 1 mm slot), and the walls, divider and gate grow straight up. No supports.
include <lib.scad>
rotate([-90, 0, 0]) translate([0, -back_out, 0]) case();
