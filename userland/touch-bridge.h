#ifndef LILAC_TOUCH_BRIDGE_H
#define LILAC_TOUCH_BRIDGE_H

static int lilac_touch_bridge_configure(void)
{
    static const char library[] = "/lilac-probe/lib/liblilac.touch.dylib";
    static const char configuration[] = "/lilac-probe/lib/liblilac.touch.dylib backboardd";
    char current[512] = {0};
    size_t length = sizeof(current);
    struct stat information;
    if (stat(library, &information) || (information.st_mode & S_IFMT) != S_IFREG ||
        sysctlbyname("kern.exec_inject", current, &length, NULL, 0) ||
        !length || length > sizeof(current) || current[length - 1] != 0) return 0;
    if (current[0] && strcmp(current, configuration)) return 0;
    if (sysctlbyname("kern.exec_inject", NULL, NULL, configuration, sizeof(configuration))) return 0;
    length = sizeof(current);
    if (sysctlbyname("kern.exec_inject", current, &length, NULL, 0) ||
        length != sizeof(configuration) || memcmp(current, configuration, sizeof(configuration))) return 0;
    puts("lilac input: verified kernel exec injection for backboardd only");
    return 1;
}
#endif
