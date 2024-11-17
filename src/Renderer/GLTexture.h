#pragma once

#include <GL/glew.h>
#include <map>
#include <string>
#include <set>
#include <sstream>
#include <iostream>
#include <bitset>
#include <variant>
#include <filesystem>
#include <Entities/TrackTextureAsset.h>
#include <LibOpenNFS.h>

#include "../Util/ImageLoader.h"

class GLTexture {
public:
    GLTexture() = default;
    explicit GLTexture(LibOpenNFS::TrackTextureAsset texture, std::vector<uint8_t> &data);
    GLubyte *GetData();

    // Utils
    static GLTexture LoadTexture(NFSVersion tag, LibOpenNFS::TrackTextureAsset &trackTexture);
    static int32_t hsStockTextureIndexRemap(int32_t textureIndex);
    static GLuint MakeTextureArray(std::map<uint32_t, GLTexture> &textures, bool repeatable);

    LibOpenNFS::TrackTextureAsset texture_asset;

private:
    std::vector<uint8_t> data;
};