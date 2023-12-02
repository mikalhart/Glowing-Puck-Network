$fa = 1;
$fs = 0.5;
VERSION = 4;
TOPVERSION = 2;
WALL = 1.5;
DEPTHBASE = 1;
COASTERDIAM = 100;
LEDBANDTHICKNESS = 2;
FUDGE = 0.2;
USB_OFF_FROM_CENTER = 3.8;
USB_RECESS = 4;
USB_WIDTH = 9;
USB_HEIGHT = 7.6;
USB_DEPTH = 2;
DEPTHBATTERY = 9;
DEPTHSHIELD = 4;
PLATFORMDEPTH = 3.5;
PCBTHICKNESS = 2.2;
BOARDWALLDEPTH = 9; // 10; // high enough to hold the board, support coaster
LEDBANDWIDTH = 13;
LEDRETAINERDEPTH = BOARDWALLDEPTH + 3;
RIMWALLDEPTH = BOARDWALLDEPTH + LEDBANDWIDTH;

TOPRIMWALLDEPTH = 10;
SPECIALOFFSET = 2.5; // A northward bias for the compartments and USB hole to avoid leaks

D0 = COASTERDIAM - 4 * WALL;    // inside the edge of the coaster
DZ = D0 - 3;                    // inside-most rim of lid
D1 = COASTERDIAM + 2 * FUDGE;   // just outside the edge of the coaster
D1a = D1 + WALL;                // coaster retainer goes from D1 to D1a
D2 = D1 + 2 * LEDBANDTHICKNESS; // outside LEDs, inside of exterior wall
D3 = D2 + 2 * WALL;             // outside exterior wall
D4 = D3 + 2 * FUDGE;            // inside lid wall
D5 = D4 + 2 * WALL;             // outside lid wall

module Bottom()
{
  difference()
  {
    union()
    {
      BottomBase();
      compartments();
      difference() { supportrim(); compartments(true); }
      difference() { outerwall(); compartments(true); }
    }
    USBHole();
  }
}

module Top()
{
  translate([0, 0, 40])
  rotate([0, 180, 0])
  {
    color("red") topouterwall();
    color("green") topinnerwall1();
    color("purple") topinnerwall2();
    color("yellow") topflattop();
    // topstackingwall(); 
  }
}

module BottomBase()
{
  difference() 
  { 
    cylinder(h=DEPTHBASE, d = D3); 
    MountingHole(DEPTHBASE); 
    brassbutton(-D0/3, 0, 90, true);
    brassbutton(0, 0, 0, true);
  } // floor

  brassbutton(-D0/3, 0, 90, false);
  brassbutton(0, 0, 0, false);

  // Version indicators
  for (i=[1:VERSION])
    rotate([0, 0, 185 + 5 * i])
      translate([D0 / 2 - 4, 0, 0])
        cylinder(h=DEPTHBASE + 1, d = 2);
}

module pieSlice(a, r, h)
{
  // a:angle, r:radius, h:height
  rotate_extrude(angle=a) square([r,h]);
}

module brassbutton(x, y, angle, negative = false)
{
  BRASSDIAM = 16;
  BRASSDEPTH = 1;
  LIPWIDTH = 2;
  BRACKETDEPTH = 2.5;
  rotate([0, 0, angle])
  translate([x, y, 0])
  {
    if (negative)
    {
      cylinder(h=BRACKETDEPTH, d=BRASSDIAM - 2 * LIPWIDTH);
    }
    else
    {
      difference()
      {
        cylinder(h=BRACKETDEPTH, d=BRASSDIAM+2*WALL);
        cylinder(h=BRACKETDEPTH, d=BRASSDIAM+2*FUDGE);
        for (i=[0:90:270])
        rotate([0, 0, i-45/2])
          pieSlice(45, BRASSDIAM+2*WALL, BRACKETDEPTH);
      }
    }
  }
}

LIPDEPTH = 2.5; // for stacking coasters
module topouterwall()
{
  difference()
  {
    cylinder(h=TOPRIMWALLDEPTH, d = D5);
    
    // We'll make the removed cylinder taper slightly inward
    cylinder(h=TOPRIMWALLDEPTH+0.1, d1 = D4 - 2 * FUDGE, d2 = D4);
  }
}

// Not used
module topstackingwall()
{
  translate([0, 0, -LIPDEPTH])
  difference()
  {
    cylinder(h=LIPDEPTH, d = D5);
    cylinder(h=LIPDEPTH, d = D4);
  }
}

INNERWALLDEPTH1 = 2;
INNERWALLDEPTH2 = 3;
module topinnerwall1()
{
  OUTER = D1a - FUDGE;
  INNER = OUTER - 2 * WALL;
  DEPTH = DEPTHBASE + INNERWALLDEPTH1;
  difference()
  {
    cylinder(h=DEPTH, d = OUTER);
    cylinder(h=DEPTH+0.1, d = INNER);
  }
}

module topinnerwall2()
{
  OUTER = D0;
  INNER = DZ;
  DEPTH = DEPTHBASE + INNERWALLDEPTH2;
  difference()
  {
    cylinder(h=DEPTH, d = OUTER);
    cylinder(h=DEPTH+0.1, d = INNER);
  }
}

module topflattop()
{
  difference()
  {
    cylinder(h=DEPTHBASE, d = D5);
    cylinder(h=DEPTHBASE+0.1, d = DZ);
  }
  DEP = 1;
  DIA = 2;
  if (0) for (i=[1, TOPVERSION])
  {
    rotate([0, 0, 180 + 5 * i])
    translate([(D5 + D0) / 4, 0, -DEP])
    cylinder(h = DEP, d = DIA);
  }
}

module outerwall()
{
  difference()
  {
    cylinder(h=RIMWALLDEPTH, d = D3);
    cylinder(h=RIMWALLDEPTH, d = D2);
  }
}

// Shelf that supports coaster and brackets it
module supportrim()
{
  difference()
  {
    cylinder(h=BOARDWALLDEPTH, d = D2);
    cylinder(h=BOARDWALLDEPTH, d = D0);
  }

  // LED retainer
  difference()
  {
    cylinder(h=LEDRETAINERDEPTH, d = D1a);
    cylinder(h=LEDRETAINERDEPTH, d = D1);
  }
}

module compartments(negative = false)
{
  BOARDWIDTH = 39.5; // 39.1; // 40.1;
  BOARDHEIGHT = 31.5;
  CONNECTORWIDTH = 8;

  PILLAROFFSETX = 2.5;
  PILLAROFFSETY1 = 2.4;
  PILLAROFFSETY2 = 2.9;
  PILLARDIAMTOP = 1.7; // 1.7;
  PILLARDIAMBASE = 2.0; // 1.7;
  PILLARDEPTH = 4.0; // 3.0;

  BATTERYWIDTH = 36.0; // 37.0; // 38.0;
  BATTERYHEIGHT = 25.0; // 25.5; // 26.0;
  BATTERYDEPTH = 8;

  CABLEEXTRA = 27;
  CABLEWIDTH = 6;

  RWALL = D1 / 2 - .7;
  x0 = RWALL - BOARDWIDTH - WALL;
  x1 = x0 + WALL;
  x1a = x1 + 15;
  x2 = x1 + 25;
  x3 = RWALL;
  x4 = x3 + WALL;
  
  y0 = -BOARDHEIGHT / 2 - USB_OFF_FROM_CENTER - WALL + SPECIALOFFSET;
  y1 = y0 + WALL;
  y3 = BOARDHEIGHT / 2 - USB_OFF_FROM_CENTER + SPECIALOFFSET;
  y2 = y3 - CONNECTORWIDTH;
  y4 = y3 + WALL;
  
  a0 = x0 - CABLEEXTRA;
  a1 = a0 + WALL;
  a2 = a1 + BATTERYWIDTH;
  a3 = a2 + WALL;
  
  b0 = y3;
  b1 = b0 + WALL;
  b2 = b1 + BATTERYHEIGHT;
  b3 = b2 + WALL;

  if (negative)
  {
    translate([x1, y1, 0])
      cube([x3-x1, y3-y1, LEDRETAINERDEPTH]);
    SWITCHOFFSET = 5;
    translate([x3, y1 + SWITCHOFFSET, 0])
      cube([x4-x3, y3-y1-SWITCHOFFSET, LEDRETAINERDEPTH]);
  }
  else
  intersection()
  {
    union()
    {
      // Board stuff
      translate([x0, y3-10, 0]) // top of left wall
        cube([x1-x0, 10, BOARDWALLDEPTH]);
      translate([x0, y1, 0]) // bottom of left wall
        cube([x1-x0, 10, BOARDWALLDEPTH]);
      translate([x0, y0, 0]) // bottom wall
        cube([x1a-x0, y1-y0, BOARDWALLDEPTH]);
      translate([x0, y3, 0]) // top wall
        cube([x1a-x0, y4-y3, BOARDWALLDEPTH]);
      translate([x3, y0, 0]) // right wall
        cube([x4-x3, y4-y0, PLATFORMDEPTH + PCBTHICKNESS/*BOARDWALLDEPTH*/]);

      translate([x1, y3-5, 0]) // platform 1 (upper left)
        cube([x2-x1, 5, PLATFORMDEPTH]);
      translate([x1, y1, 0]) // platform 2 (lower left)
        cube([x3-x1, 5, PLATFORMDEPTH]);
      translate([x3-3, y1, 0]) // platform 3 (right)
        cube([3, y2 - y1, PLATFORMDEPTH]);
      translate([x1 + PILLAROFFSETX, y1 + PILLAROFFSETY1, PLATFORMDEPTH]) // pillar 1
        cylinder(h = PILLARDEPTH, d1 = PILLARDIAMBASE, d2 = PILLARDIAMTOP);
      translate([x1 + PILLAROFFSETX, y3 - PILLAROFFSETY2, PLATFORMDEPTH]) // pillar 2
        cylinder(h = PILLARDEPTH, d1 = PILLARDIAMBASE, d2 = PILLARDIAMTOP);
      
      // Battery stuff
      translate([a0, b0, 0]) // left wall
        cube([a1-a0, b3-b0, BOARDWALLDEPTH]);
      translate([a0, b0, 0]) // bottom wall
        cube([a3-a0, b1-b0, BOARDWALLDEPTH]);
      translate([a0, b2, 0]) // top wall
        cube([a3-a0, b3-b2, BOARDWALLDEPTH]);
      translate([a2, b1 + CABLEWIDTH, 0]) // right wall
        cube([a3-a2, b3-(b1 + CABLEWIDTH), BOARDWALLDEPTH]);
    }
    cylinder(h=BOARDWALLDEPTH, d = D3);
  }
}

module USBHole()
{
  USB_HOLE_WIDTH = USB_WIDTH + 2 * 2;
  USB_HOLE_HEIGHT = USB_HEIGHT + 2 * 3 + WALL;
  USB_HOLE_DEPTH = USB_DEPTH + 2 * 2 + 2;
  RWALL = COASTERDIAM / 2;

  translate([RWALL - USB_HOLE_HEIGHT/2 + 0.2, 0 - USB_HOLE_WIDTH / 2 + SPECIALOFFSET, PLATFORMDEPTH + PCBTHICKNESS + USB_DEPTH / 2 - USB_HOLE_DEPTH / 2/*USB_DEPTH / 2 */])
  rotate([0, 0, 90])
  rotate([90, 0, 0])
  union()
  {
      RADIUS = USB_HOLE_DEPTH / 2;
      translate([RADIUS, RADIUS, 0])
          cylinder(h = USB_HOLE_HEIGHT, d = USB_HOLE_DEPTH);
      translate([RADIUS, 0, 0]) 
          cube([USB_HOLE_WIDTH - USB_HOLE_DEPTH, USB_HOLE_DEPTH, USB_HOLE_HEIGHT], false);
      translate([USB_HOLE_WIDTH - USB_HOLE_DEPTH + RADIUS, RADIUS, 0])
          cylinder(h = USB_HOLE_HEIGHT, d = USB_HOLE_DEPTH);
  }
}

module MountingHole(depth)
{
  HEAD4DIAM = 5.48;
  SHAFT4DIAM = 2.74;

  BIGDIAM = HEAD4DIAM + FUDGE;
  LITTLEDIAM = SHAFT4DIAM + FUDGE;

  translate([-D0/3, 0, 0])
  rotate([0, 0, 90])
  {
    cylinder(depth, d = BIGDIAM);
    translate([-LITTLEDIAM / 2, 0, 0]) 
      cube([LITTLEDIAM, BIGDIAM/2 + 3 * LITTLEDIAM / 2, depth], center = false);
    translate([0, BIGDIAM/2 + 3 * LITTLEDIAM / 2, 0])
      cylinder(depth, d = LITTLEDIAM);
  }
}