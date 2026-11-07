#include "platform/SpotifyApp.h"

#import <AppKit/AppKit.h>

namespace {
NSString *const kBundleId = @"com.spotify.client";
}

namespace SpotifyApp {
bool isRunning()
{
    return [NSRunningApplication runningApplicationsWithBundleIdentifier:kBundleId].count > 0;
}

bool launchQuietly()
{
    NSURL *url = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:kBundleId];
    if (!url)
        return false;
    NSWorkspaceOpenConfiguration *config = [NSWorkspaceOpenConfiguration configuration];
    config.activates = NO;
    config.hides = YES;
    [[NSWorkspace sharedWorkspace] openApplicationAtURL:url configuration:config completionHandler:nil];
    return true;
}

}
