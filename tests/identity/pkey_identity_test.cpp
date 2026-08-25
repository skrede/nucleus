#include "identity/pkey_identity_test_support.h"

#include "nucleus/error.h"
#include "nucleus/config.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace pkey_test = nucleus::pkey_test;

TEST_CASE("attribute-form pkey retained as readable leaf", "[pkey_identity]")
{
    const nucleus::config_space space  = pkey_test::cluster_space();
    const nucleus::load_result  loaded = pkey_test::load_doc(
            space, R"(<cluster><server name="web"><port>80</port></server></cluster>)");
    REQUIRE(loaded);
    REQUIRE(loaded->get("cluster/server/name") == "web");
    REQUIRE(loaded->get("cluster/server/port") == "80");
    REQUIRE_FALSE(loaded->contains("cluster/server/web/port"));
}

TEST_CASE("text-leaf-form pkey retained as readable leaf", "[pkey_identity]")
{
    const nucleus::config_space space  = pkey_test::cluster_space();
    const nucleus::load_result  loaded = pkey_test::load_doc(
            space, R"(<cluster><server><name>web</name><port>80</port></server></cluster>)");
    REQUIRE(loaded);
    REQUIRE(loaded->get("cluster/server/name") == "web");
    REQUIRE(loaded->get("cluster/server/port") == "80");
    REQUIRE_FALSE(loaded->contains("cluster/server/web/port"));
}

TEST_CASE("anonymous strain produces no pkey leaf", "[pkey_identity]")
{
    const nucleus::config_space space  = pkey_test::cluster_space();
    const nucleus::load_result  loaded = pkey_test::load_doc(
            space, R"(<cluster><server><port>80</port></server></cluster>)");
    REQUIRE(loaded);
    REQUIRE_FALSE(loaded->contains("cluster/server/name"));
}

TEST_CASE("schema validation accepts a retained pkey leaf", "[pkey_identity]")
{
    const nucleus::config_space space  = pkey_test::cluster_space();
    const nucleus::load_result  loaded = pkey_test::load_doc(
            space, R"(<cluster><server name="web"><port>80</port></server></cluster>)");
    REQUIRE(loaded);
    REQUIRE(loaded->get("cluster/server/name") == "web");
}

TEST_CASE("required pkey on anonymous strain fails validation", "[pkey_identity]")
{
    const nucleus::config_space space  = pkey_test::cluster_space(true);
    const nucleus::load_result  loaded = pkey_test::load_doc(
            space, R"(<cluster><server><port>80</port></server></cluster>)");
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::schema_violation);
    REQUIRE(loaded.error().message.find("cluster/server/name") != std::string::npos);
}
