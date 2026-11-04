#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Just as much JSON as greetd speaks: objects of strings and of lists of strings out, objects
// of strings back.
namespace charon::json {

// `text` as a JSON string, quotes and all.
std::string quoted(std::string_view text);
// A list of strings.
std::string list(const std::vector<std::string>& items);

// The strings of a flat object, by key; what is not a string, as a number, is passed by. Empty
// when `text` is no object.
std::optional<std::map<std::string, std::string>> parseObject(std::string_view text);

} // namespace charon::json
