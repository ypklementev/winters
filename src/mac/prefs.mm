#include "mac/prefs.h"

#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

namespace {

int ReadInt(NSUserDefaults* d, NSString* key, int def) {
    id v = [d objectForKey:key];
    return [v respondsToSelector:@selector(intValue)] ? [v intValue] : def;
}

}  // namespace

sim::UserSettings LoadSettings() {
    NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
    sim::UserSettings s;
    s.intensity = static_cast<sim::Intensity>(ReadInt(d, @"Intensity", static_cast<int>(s.intensity)));
    s.windDirection = static_cast<sim::WindDirection>(ReadInt(d, @"WindDirection", static_cast<int>(s.windDirection)));
    s.windStrength = ReadInt(d, @"WindStrength", s.windStrength);
    s.wiper = ReadInt(d, @"Wiper", 0) != 0;
    s.wiperPlaced = ReadInt(d, @"WiperPlaced", 0) != 0;
    s.wiperX = ReadInt(d, @"WiperX", 0);
    s.wiperY = ReadInt(d, @"WiperY", 0);
    s.unlimited = ReadInt(d, @"Unlimited", 0) != 0;
    return sim::sanitize(s);
}

void SaveSettings(const sim::UserSettings& s) {
    NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
    [d setInteger:static_cast<int>(s.intensity) forKey:@"Intensity"];
    [d setInteger:static_cast<int>(s.windDirection) forKey:@"WindDirection"];
    [d setInteger:s.windStrength forKey:@"WindStrength"];
    [d setInteger:s.wiper ? 1 : 0 forKey:@"Wiper"];
    [d setInteger:s.wiperPlaced ? 1 : 0 forKey:@"WiperPlaced"];
    [d setInteger:s.wiperX forKey:@"WiperX"];
    [d setInteger:s.wiperY forKey:@"WiperY"];
    [d setInteger:s.unlimited ? 1 : 0 forKey:@"Unlimited"];
}

LoginItemState GetLoginItemState() {
    switch (SMAppService.mainAppService.status) {
        case SMAppServiceStatusEnabled:
            return LoginItemState::Enabled;
        case SMAppServiceStatusRequiresApproval:
            return LoginItemState::RequiresApproval;
        case SMAppServiceStatusNotRegistered:
            return LoginItemState::Disabled;
        case SMAppServiceStatusNotFound:
        default:
            return LoginItemState::Unavailable;
    }
}

bool SetLoginItem(bool enabled) {
    NSError* err = nil;
    SMAppService* svc = SMAppService.mainAppService;
    const BOOL ok = enabled ? [svc registerAndReturnError:&err] : [svc unregisterAndReturnError:&err];
    if (!ok) NSLog(@"WinTers: login item %@ failed: %@", enabled ? @"register" : @"unregister", err);
    return ok;
}

void OpenLoginItemsSettings() { [SMAppService openSystemSettingsLoginItems]; }
