#include "functions.h"
#include "cameraFunc.c"
void main ()
{

    open_device ();
    init_device ();
    start_capturing ();
    run ();
    uninit_device();
    close_device();
}
