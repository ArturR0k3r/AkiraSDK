#include "akira_api.h"

int main(void)
{
    display_clear(COLOR_BLACK);
    display_text(10, 10, "Hello from __APP_NAME__", COLOR_CYAN);
    display_flush();
    return 0;
}
