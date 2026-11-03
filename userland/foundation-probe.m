/* SPDX-License-Identifier: BSD-2-Clause */
#import <Foundation/Foundation.h>
#include <sys/mount.h>
#include <stdio.h>
#include <unistd.h>
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);puts("FOUNDATION START: Objective-C + original shared cache");
    @autoreleasepool {
        NSError *error=nil;
        NSDictionary *object=@{@"platform":@"lilac",@"answer":@42,@"items":@[@1,@2,@3]};
        NSData *json=[NSJSONSerialization dataWithJSONObject:object options:0 error:&error];
        NSDictionary *back=json?[NSJSONSerialization JSONObjectWithData:json options:0 error:&error]:nil;
        BOOL jsonOK=back && [back isEqual:object];
        NSData *plist=[NSPropertyListSerialization dataWithPropertyList:object format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
        NSDictionary *parsed=plist?[NSPropertyListSerialization propertyListWithData:plist options:0 format:NULL error:&error]:nil;
        BOOL plistOK=parsed && [parsed isEqual:object];
        NSString *string=[NSString stringWithFormat:@"%@-%d",@"Darwin",42];
        BOOL textOK=[string isEqualToString:@"Darwin-42"] && [@"索尼" length]==2;
        struct statfs fs;BOOL fileOK=NO;
        if(statfs("/private/var/tmp",&fs)==0 && !strcmp(fs.f_fstypename,"ramfs")) {
            NSString *path=@"/private/var/tmp/lilac-foundation.plist";
            fileOK=[plist writeToFile:path options:NSDataWritingAtomic error:&error] &&
                [[NSData dataWithContentsOfFile:path] isEqual:plist];
            [[NSFileManager defaultManager] removeItemAtPath:path error:NULL];
        }
        printf("FOUNDATION JSON=%s plist=%s Unicode=%s atomic-file=%s\n",jsonOK?"PASS":"FAIL",plistOK?"PASS":"FAIL",textOK?"PASS":"FAIL",fileOK?"PASS":"FAIL");
        BOOL ok=jsonOK && plistOK && textOK && fileOK;
        printf("FOUNDATION BATCH: %s\n",ok?"PASS":"FAIL");return ok?0:1;
    }
}
