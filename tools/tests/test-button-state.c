#include <assert.h>
#include <stdio.h>
#include "../../kernel/lilac-button-state.h"

int main(void)
{
    struct lilac_button_state s = {0};
    assert(!lilac_button_sample(&s, true, true));
    assert(!lilac_button_sample(&s, true, true));
    assert(!s.armed);
    assert(!lilac_button_sample(&s, true, false));
    assert(!lilac_button_sample(&s, true, false));
    assert(s.armed);
    assert(!lilac_button_sample(&s, true, true));
    assert(!lilac_button_sample(&s, false, false));
    assert(!lilac_button_sample(&s, true, true));
    assert(lilac_button_sample(&s, true, true) == 1);
    assert(!lilac_button_sample(&s, true, true));
    assert(!lilac_button_sample(&s, true, false));
    assert(!lilac_button_sample(&s, true, true));
    assert(!lilac_button_sample(&s, true, false));
    assert(lilac_button_sample(&s, true, false) == -1);
    assert(!lilac_button_sample(&s, true, false));
    puts("button state: passed");
    return 0;
}
