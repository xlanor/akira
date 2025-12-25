#version 460

layout (location = 0) in vec2 vTexCoord;
layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D plane0;  // Y (luma) plane
layout (binding = 1) uniform sampler2D plane1;  // UV (chroma) plane

// NV12 to RGB conversion
// Y is full range [0,255], UV is [0,255] with 128 as neutral
void main()
{
#ifdef DEBUG_DEKO3D_RENDERING
    // DEBUG: Draw a small green square in top-left corner to confirm quad is rendering
    if (vTexCoord.x < 0.1 && vTexCoord.y < 0.1)
    {
        outColor = vec4(0.0, 1.0, 0.0, 1.0);  // Green
        return;
    }

    // DEBUG: Draw a small blue square in top-right corner to show texture coords work
    if (vTexCoord.x > 0.9 && vTexCoord.y < 0.1)
    {
        outColor = vec4(0.0, 0.0, 1.0, 1.0);  // Blue
        return;
    }
#endif

    float y = texture(plane0, vTexCoord).r;
    float u = texture(plane1, vTexCoord).r - 0.5;
    float v = texture(plane1, vTexCoord).g - 0.5;

    // BT.601 limited range to RGB conversion
    float r = y + 1.13983 * v;
    float g = y - 0.39465 * u - 0.58060 * v;
    float b = y + 2.03211 * u;

    outColor = vec4(r, g, b, 1.0);
}
