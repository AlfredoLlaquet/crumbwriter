// Original Crumb Writer source, dedicated under the Unlicense.
// The separate executable linked with Poppler is distributed under its GPL terms.
#include <poppler-document.h>
#include <poppler-page.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main()
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try {
        // The parent sets the working directory to the installed app. Relative
        // paths keep Poppler's encoding-map lookup independent of Windows ANSI paths.
        if (std::filesystem::is_directory("share/poppler") && !poppler::set_data_dir("share/poppler"))
            throw std::runtime_error("Could not select the installed Poppler data.");
        std::istreambuf_iterator<char> start(std::cin), end;
        poppler::byte_array data(start, end);
        std::unique_ptr<poppler::document> doc(poppler::document::load_from_data(&data));
        if (!doc) throw std::runtime_error("Could not open the PDF.");
        if (doc->is_locked()) throw std::runtime_error("The PDF requires a password.");
        if (!doc->has_permission(poppler::perm_copy)) throw std::runtime_error("The PDF restricts text copying.");
        for (int i = 0; i < doc->pages(); ++i) {
            std::unique_ptr<poppler::page> page(doc->create_page(i));
            if (!page) throw std::runtime_error("Could not read a PDF page.");
            auto text = page->text({}, poppler::page::non_raw_non_physical_layout).to_utf8();
            while (!text.empty() && (text.back() == '\f' || text.back() == '\r' || text.back() == '\n'))
                text.pop_back();
            if (i) std::cout << "\n\n";
            std::cout.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        return std::cout ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
