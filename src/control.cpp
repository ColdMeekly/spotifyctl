#include "spotify/control.h"

namespace spotify::uri {

namespace {

std::string PercentEncode(std::string_view in) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        const bool unreserved =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

}  // namespace

std::string Track(std::string_view id)      { return "spotify:track:"    + std::string(id); }
std::string Album(std::string_view id)      { return "spotify:album:"    + std::string(id); }
std::string Playlist(std::string_view id)   { return "spotify:playlist:" + std::string(id); }
std::string Artist(std::string_view id)     { return "spotify:artist:"   + std::string(id); }
std::string User(std::string_view username) { return "spotify:user:"     + std::string(username); }
std::string Search(std::string_view query)  { return "spotify:search:"   + PercentEncode(query); }

}  // namespace spotify::uri
