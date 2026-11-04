#ifndef LILAC_TOUCH_BRIDGE_H
#define LILAC_TOUCH_BRIDGE_H

static int lilac_touch_bridge_configure(int desktop_bringup)
{
    static const char library[] = "/lilac-probe/lib/liblilac.touch.dylib";
    const char *configuration = desktop_bringup ?
        "/lilac-probe/lib/liblilac.touch.dylib backboardd SpringBoard" :
        "/lilac-probe/lib/liblilac.touch.dylib backboardd";
    size_t configuration_bytes = strlen(configuration) + 1;
    char current[512] = {0};
    size_t length = sizeof(current);
    struct stat information;
    if (stat(library, &information) || (information.st_mode & S_IFMT) != S_IFREG ||
        sysctlbyname("kern.exec_inject", current, &length, NULL, 0) ||
        !length || length > sizeof(current) || current[length - 1] != 0) return 0;
    if (current[0] && strcmp(current, configuration)) return 0;
    if (sysctlbyname("kern.exec_inject", NULL, NULL, configuration, configuration_bytes)) return 0;
    length = sizeof(current);
    if (sysctlbyname("kern.exec_inject", current, &length, NULL, 0) ||
        length != configuration_bytes || memcmp(current, configuration, configuration_bytes)) return 0;
    puts(desktop_bringup ? "lilac input: verified injection for backboardd and RAM desktop SpringBoard" :
        "lilac input: verified kernel exec injection for backboardd only");
    return 1;
}
#endif
