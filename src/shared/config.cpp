#include "config.h"

#include "version.h"

#include <iostream>

namespace vNerve::bilibili::config
{
config_t parse_options(int argc, char** argv)
{
    using namespace boost::program_options;
    auto desc = create_description();
    std::shared_ptr<variables_map> result = std::make_shared<variables_map>();
    try
    {
        store(parse_command_line(argc, argv, desc), *result);
    }
    catch (boost::program_options::unknown_option& ex)
    {
        std::cerr << ex.what() << std::endl;
        std::cerr << desc << std::endl;
        std::exit(-1);
    }

    if (result->count("help"))
    {
        std::cerr << desc << std::endl;
        std::exit(-1);
    }

    if (result->count("version"))
    {
        std::cerr << VERSION << std::endl;
        std::exit(-1);
    }
    return result;
}
}  // namespace vNerve::bilibili::config
