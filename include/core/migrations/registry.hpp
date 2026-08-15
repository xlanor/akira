#pragma once

#include <tomlmigrate/tomlmigrate.hpp>

#include "m000_baseline.hpp"
#include "m001_unify_psn_auth.hpp"
#include "m002_multi_profile.hpp"
#include "m003_drop_hardened_nat.hpp"

namespace chiaki_migrations {

inline tomlmigrate::Migrator buildSettingsMigrator() {
    tomlmigrate::Options opts;
    opts.version_key = "version";
    opts.assume_missing_version = 0;

    tomlmigrate::Migrator m(opts);
    register_m000_baseline(m);
    register_m001_unify_psn_auth(m);
    register_m002_multi_profile(m);
    register_m003_drop_hardened_nat(m);
    return m;
}

}
