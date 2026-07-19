#pragma once

#include <tomlmigrate/tomlmigrate.hpp>

#include "m000_baseline.hpp"

namespace chiaki_migrations {

inline tomlmigrate::Migrator buildSettingsMigrator() {
    tomlmigrate::Options opts;
    opts.version_key = "version";
    opts.assume_missing_version = 0;

    tomlmigrate::Migrator m(opts);
    register_m000_baseline(m);
    return m;
}

}
