#include "ShirayukiMemory.hpp"
#include <dlfcn.h>

namespace Shirayuki {

ImageInfo Image::find(const std::string &imageName) {
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        const char *name = _dyld_get_image_name(i);
        if (!name)
            continue;

        std::string path(name);
        if (path == imageName || path.find(imageName) != std::string::npos) {
            ImageInfo info;
            info.name = path;
            info.base = (uintptr_t)_dyld_get_image_header(i);
            info.slide = _dyld_get_image_vmaddr_slide(i);
            return info;
        }
    }
    return {};
}

ImageInfo Image::getBase() {
    ImageInfo info;
    info.name = _dyld_get_image_name(0);
    info.base = (uintptr_t)_dyld_get_image_header(0);
    info.slide = _dyld_get_image_vmaddr_slide(0);
    return info;
}

std::vector<ImageInfo> Image::listAll() {
    std::vector<ImageInfo> images;
    uint32_t count = _dyld_image_count();
    images.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        ImageInfo info;
        info.name = _dyld_get_image_name(i) ?: "";
        info.base = (uintptr_t)_dyld_get_image_header(i);
        info.slide = _dyld_get_image_vmaddr_slide(i);
        images.push_back(info);
    }
    return images;
}

uintptr_t Image::absoluteAddress(const ImageInfo &img, uintptr_t offset) {
    if (!img.isValid())
        return 0;
    return img.base + offset;
}

uintptr_t Image::absoluteAddress(const std::string &imageName, uintptr_t offset) {
    return absoluteAddress(find(imageName), offset);
}

uintptr_t Image::findSymbol(const std::string &imageName, const std::string &symbolName) {
    void *handle = dlopen(imageName.c_str(), RTLD_NOLOAD);
    if (!handle)
        return 0;
    void *sym = dlsym(handle, symbolName.c_str());
    dlclose(handle);
    return (uintptr_t)sym;
}

uintptr_t Image::findSymbol(const ImageInfo &img, const std::string &symbolName) {
    return findSymbol(img.name, symbolName);
}

} // namespace Shirayuki
