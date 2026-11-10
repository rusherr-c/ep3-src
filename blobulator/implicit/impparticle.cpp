//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#ifdef IS_WINDOWS_PC
#include <windows.h>
#include <gl/gl.h>
#include <gl/glu.h>
#endif
#include "../util/rand.h"
#include "blobulator/implicit/impparticle.h"

#include "memdbgon.h"

#if 0
// Old initializer code
interpolants2 = Point3D(frand2(),frand2(),frand2());
interpolants2.normalize();
interpolants3 = Point3D(frand2(),frand2(),frand2());
interpolants3.normalize();
interpolants4 = interpolants2.crossProduct(interpolants3);
interpolants3 = interpolants2.crossProduct(interpolants4);
interpolants3.normalize();
interpolants4.normalize();
#endif


