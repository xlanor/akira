#pragma once

#include <tomlmigrate/tomlmigrate.hpp>

#include "m000_baseline.hpp"
#include "m001_unify_psn_auth.hpp"

namespace chiaki_migrations {

inline tomlmigrate::Migrator buildSettingsMigrator() {
    tomlmigrate::Options opts;
    opts.version_key = "version";
    opts.assume_missing_version = 0;

    tomlmigrate::Migrator m(opts);
    register_m000_baseline(m);
    register_m001_unify_psn_auth(m);
    return m;
}

}
