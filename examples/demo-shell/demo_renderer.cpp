/*
 * Copyright © 2014 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Daniel van Vugt <daniel.van.vugt@canonical.com>
 */

#include "demo_renderer.h"
#include <mir/graphics/renderable.h>
#include <cmath>

using namespace mir;
using namespace mir::examples;

namespace
{

float shadow_curve(float x)
{
    return 1.0f - sinf(x * M_PI / 2.0f);
}

void generate_shadow_textures(float opacity, GLuint& edge, GLuint& corner)
{
    typedef struct
    {
        GLubyte luminance;
        GLubyte alpha;
    } Texel;

    const int width = 256;
    Texel image[width][width];
    for (int y = 0; y < width; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            Texel *t = &image[y][x];
            t->luminance = 0;
            t->alpha = opacity * 255.0f *
                shadow_curve((float)x / width) *
                shadow_curve((float)y / width);
        }
    }

    glGenTextures(1, &edge);
    glBindTexture(GL_TEXTURE_2D, edge);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
                 width, 1, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                 image);

    glGenTextures(1, &corner);
    glBindTexture(GL_TEXTURE_2D, corner);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
                 width, width, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                 image);
}

} // namespace

DemoRenderer::DemoRenderer(geometry::Rectangle const& display_area)
    : GLRenderer(display_area)
{
    generate_shadow_textures(0.4f, shadow_edge_tex, shadow_corner_tex);
}

void DemoRenderer::begin() const
{
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void DemoRenderer::tessellate(graphics::Renderable const& renderable,
                              std::vector<Primitive>& primitives) const
{
    GLRenderer::tessellate(renderable, primitives);

    auto const& rect = renderable.screen_position();
    GLfloat left = rect.top_left.x.as_int();
    GLfloat right = left + rect.size.width.as_int();
    GLfloat top = rect.top_left.y.as_int();
    GLfloat bottom = top + rect.size.height.as_int();

    float radius = 80.0f; // TODO configurable?

    auto n = primitives.size();
    primitives.resize(n + 8);

    auto& right_shadow = primitives[n++];
    right_shadow.tex_id = shadow_edge_tex;
    right_shadow.type = GL_TRIANGLE_FAN;
    right_shadow.vertices.resize(4);
    right_shadow.vertices[0] = {{right,          top,    0.0f}, {0.0f, 0.0f}};
    right_shadow.vertices[1] = {{right + radius, top,    0.0f}, {1.0f, 0.0f}};
    right_shadow.vertices[2] = {{right + radius, bottom, 0.0f}, {1.0f, 1.0f}};
    right_shadow.vertices[3] = {{right,          bottom, 0.0f}, {0.0f, 1.0f}};

    auto& left_shadow = primitives[n++];
    left_shadow.tex_id = shadow_edge_tex;
    left_shadow.type = GL_TRIANGLE_FAN;
    left_shadow.vertices.resize(4);
    left_shadow.vertices[0] = {{left - radius, top,    0.0f}, {1.0f, 1.0f}};
    left_shadow.vertices[1] = {{left,          top,    0.0f}, {0.0f, 1.0f}};
    left_shadow.vertices[2] = {{left,          bottom, 0.0f}, {0.0f, 0.0f}};
    left_shadow.vertices[3] = {{left - radius, bottom, 0.0f}, {1.0f, 0.0f}};

    auto& top_shadow = primitives[n++];
    top_shadow.tex_id = shadow_edge_tex;
    top_shadow.type = GL_TRIANGLE_FAN;
    top_shadow.vertices.resize(4);
    top_shadow.vertices[0] = {{left,  top - radius, 0.0f}, {1.0f, 0.0f}};
    top_shadow.vertices[1] = {{right, top - radius, 0.0f}, {1.0f, 1.0f}};
    top_shadow.vertices[2] = {{right, top,          0.0f}, {0.0f, 1.0f}};
    top_shadow.vertices[3] = {{left,  top,          0.0f}, {0.0f, 0.0f}};

    auto& bottom_shadow = primitives[n++];
    bottom_shadow.tex_id = shadow_edge_tex;
    bottom_shadow.type = GL_TRIANGLE_FAN;
    bottom_shadow.vertices.resize(4);
    bottom_shadow.vertices[0] = {{left,  bottom,          0.0f}, {0.0f, 1.0f}};
    bottom_shadow.vertices[1] = {{right, bottom,          0.0f}, {0.0f, 0.0f}};
    bottom_shadow.vertices[2] = {{right, bottom + radius, 0.0f}, {1.0f, 0.0f}};
    bottom_shadow.vertices[3] = {{left,  bottom + radius, 0.0f}, {1.0f, 1.0f}};

    auto& tr_shadow = primitives[n++];
    tr_shadow.tex_id = shadow_corner_tex;
    tr_shadow.type = GL_TRIANGLE_FAN;
    tr_shadow.vertices.resize(4);
    tr_shadow.vertices[0] = {{right, top,                   0.0f}, {0.0f, 0.0f}};
    tr_shadow.vertices[1] = {{right, top - radius,          0.0f}, {1.0f, 0.0f}};
    tr_shadow.vertices[2] = {{right + radius, top - radius, 0.0f}, {1.0f, 1.0f}};
    tr_shadow.vertices[3] = {{right + radius, top,          0.0f}, {0.0f, 1.0f}};

    auto& br_shadow = primitives[n++];
    br_shadow.tex_id = shadow_corner_tex;
    br_shadow.type = GL_TRIANGLE_FAN;
    br_shadow.vertices.resize(4);
    br_shadow.vertices[0] = {{right, bottom,                   0.0f}, {0.0f, 0.0f}};
    br_shadow.vertices[1] = {{right + radius, bottom,          0.0f}, {1.0f, 0.0f}};
    br_shadow.vertices[2] = {{right + radius, bottom + radius, 0.0f}, {1.0f, 1.0f}};
    br_shadow.vertices[3] = {{right, bottom + radius,          0.0f}, {0.0f, 1.0f}};

    auto& bl_shadow = primitives[n++];
    bl_shadow.tex_id = shadow_corner_tex;
    bl_shadow.type = GL_TRIANGLE_FAN;
    bl_shadow.vertices.resize(4);
    bl_shadow.vertices[0] = {{left, bottom,                   0.0f}, {0.0f, 0.0f}};
    bl_shadow.vertices[1] = {{left, bottom + radius,          0.0f}, {1.0f, 0.0f}};
    bl_shadow.vertices[2] = {{left - radius, bottom + radius, 0.0f}, {1.0f, 1.0f}};
    bl_shadow.vertices[3] = {{left - radius, bottom,          0.0f}, {0.0f, 1.0f}};

    auto& tl_shadow = primitives[n++];
    tl_shadow.tex_id = shadow_corner_tex;
    tl_shadow.type = GL_TRIANGLE_FAN;
    tl_shadow.vertices.resize(4);
    tl_shadow.vertices[0] = {{left, top,                   0.0f}, {0.0f, 0.0f}};
    tl_shadow.vertices[1] = {{left - radius, top,          0.0f}, {1.0f, 0.0f}};
    tl_shadow.vertices[2] = {{left - radius, top - radius, 0.0f}, {1.0f, 1.0f}};
    tl_shadow.vertices[3] = {{left, top - radius,          0.0f}, {0.0f, 1.0f}};

    // Shadows always need blending...
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
