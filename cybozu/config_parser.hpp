// Read and parse configuration files.
// (C) 2013 Cybozu.

#ifndef CYBOZU_CONFIG_PARSER_HPP
#define CYBOZU_CONFIG_PARSER_HPP

#include <unordered_map>
#include <string>
#include <stdexcept>
#include <cstdint>

namespace cybozu {

// Parse configuration files.
class config_parser {
    std::unordered_map<std::string, std::string> m_config;

public:
    // Create an empty config_parser object.
    config_parser() {}

    // Create a <config_parser> and load the file at `path`.
    // @path  The path to the configuration file.
    //
    // Create a <config_parser> and load the file at `path`.
    // Raise <parse_error> exception if the file contains an invalid line.
    explicit config_parser(const std::string& path) {
        load(path);
    }

    // exception for invalid file format.
    struct parse_error: public std::runtime_error {
        parse_error(const std::string& path, unsigned int lineno):
            std::runtime_error("Parse error in " + path + " at line " +
                               std::to_string(lineno)) {}
    };

    // exception for key not found error.
    struct not_found: public std::runtime_error {
        not_found(const std::string& key):
            std::runtime_error("Key not found: " + key) {}
    };

    // exception for illegal value error.
    struct illegal_value: public std::runtime_error {
        illegal_value(const std::string& key):
            std::runtime_error("Illegal value for " + key) {}
    };

    // Load a configuration file.
    // @path  The path to the configuration file.
    //
    // Load a configuration file.
    // Raise <parse_error> exception if the file contains an invalid line.
    // All previously loaded configurations will be cleared.
    void load(const std::string& path);

    // Set a configuration value.
    // @key   A configuration key.
    // @value The associated value.
    void set(const std::string& key, const std::string& value) {
        auto it = m_config.find(key);
        if( it == m_config.end() ) {
            m_config.emplace(key, value);
        } else {
            it->second = value;
        }
    }

    // Get a value associated with `key`.
    // @key   A configuration key.
    //
    // Get a value associated with `key`.
    // Raise <not_found> exception if the key is not found.
    //
    // @return A reference to the <std::string> associated with `key`.
    const std::string& get(const std::string& key) const {
        auto it = m_config.find(key);
        if( it == m_config.end() )
            throw not_found(key);
        return it->second;
    }

    // Return `true` if `key` exists.
    // @key   A configuration key.
    bool exists(const std::string& key) const {
        return m_config.find(key) != m_config.end();
    }

    // Get an integer converted from the value associated with `key`.
    // @key   A configuration key.
    //
    // Get an integer converted from the value associated with `key`.
    // Raise <not_found> or <illegal_value>.
    //
    // @return An integer converted from the associated value.
    int get_as_int(const std::string& key) const {
        try {
            return std::stoi(get(key));
        } catch(const std::invalid_argument& e) {
            throw illegal_value(key);
        } catch(const std::out_of_range& e) {
            throw illegal_value(key);
        }
    }

    // Get an uint64_t integer converted from the value associated with `key`.
    // @key   A configuration key.
    //
    // Get an uint64_t integer converted from the value associated with `key`.
    // Raise <not_found> or <illegal_value>.
    //
    // @return An uint64_t integer converted from the associated value.
    std::uint64_t get_as_uint64(const std::string& key) const {
        try {
            return std::stoull(get(key));
        } catch(const std::invalid_argument& e) {
            throw illegal_value(key);
        } catch(const std::out_of_range& e) {
            throw illegal_value(key);
        }
    }

    // Get a boolean converted from the value associated with `key`.
    // @key   A configuration key.
    //
    // Get a boolean converted from the value associated with `key`.
    // Raise <not_found> or <illegal_value>.
    //
    //@return `true` or `false` converted from the associated value.
    bool get_as_bool(const std::string& key) const {
        const std::string& s = get(key);
        if( s == "true" ) return true;
        if( s == "false" ) return false;
        throw illegal_value(key);
    }
};

} // namespace cybozu

#endif // CYBOZU_CONFIG_PARSER_HPP
