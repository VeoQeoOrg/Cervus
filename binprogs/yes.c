#include "../apps/cervus_user.h"

CERVUS_MAIN(yes_main) {
    const char *msg = (argc>=2) ? argv[1] : "y";
    for(;;){
        ws(msg);
        write(1,"\n",1);
    }
}