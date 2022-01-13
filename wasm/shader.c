#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "shader.h"
#include "wasm_utils.h"

extern bool uses_gl(void);

static const char *vertex_shader_100 = "#version 100 \n\
attribute vec4 aPosition;\n\
void main(void) {\n\
gl_Position = aPosition;\n\
}\n\
";

static const char *vertex_shader_300 = "#version 300 es\n\
in vec4 aPosition;\n\
void main(void) {\n\
gl_Position = aPosition;\n\
}\n\
";

uint16_t get_gl_version() {
    GLint major = 0, minor = 0;

    char *version = (char *) glGetString(GL_VERSION);

    int res = sscanf(version, "OpenGL ES %d.%d", &major, &minor);

    if (res != 2) {
        // Maybe the OpenGL ES prefixed was missing
        res = sscanf(version, "%d.%d", &major, &minor);
    }

    if (res != 2) {
        major = 0;
        minor = 0;
    }

    return (uint16_t)(major * 0x100 + minor);
}

static GLuint create_shader(const char *source, GLenum type)
{
    // Create the shader object
    printf("Creating shader...\n");
    GLuint shader = glCreateShader(type);

    // Load the shader source
    glShaderSource(shader, 1, &source, 0);

    // Compile the shader
    printf("Compiling shader...\n");
    glCompileShader(shader);

    // Check for errors
    printf("Checking for errors...\n");

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

    if (status == GL_FALSE) {
        GLint info_len = 0;

        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);

        if (info_len > 1) {
            char* info_log = (char*)malloc(sizeof(char) * info_len);

            glGetShaderInfoLog(shader, info_len, NULL, info_log);
            fprintf(stderr, "GLSL Shader Error: \n%s\n", info_log);

            free(info_log);
        }

        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static GLuint create_program(const char *vsh, const char *fsh)
{
    // Build shaders
    GLuint vertex_shader = create_shader(vsh, GL_VERTEX_SHADER);
    if (vertex_shader == 0) return 0;
    GLuint fragment_shader = create_shader(fsh, GL_FRAGMENT_SHADER);
    if (fragment_shader == 0) return 0;

    // Create program
    GLuint program = glCreateProgram();

    printf("Creating program...\n");

    // Attach shaders
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);

    printf("Linking program...\n");

    // Link program
    glLinkProgram(program);

    printf("Checking for errors...\n");

    // Check for errors
    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);

    if (status == GL_FALSE) {
        GLint info_len = 0;

        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_len);

        if (info_len > 1) {
            char* info_log = (char*)malloc(sizeof(char) * info_len);

            glGetProgramInfoLog(program, info_len, NULL, info_log);
            fprintf(stderr, "Error linking program:\n%s\n", info_log);
            fprintf(stderr, "Vertex Shader:\n%s\n", vsh);
            fprintf(stderr, "Fragment Shader:\n%s\n", fsh);

            free(info_log);
        }

        glDeleteProgram(program);
        program = 0;
    }

    // Delete shaders
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    return program;
}

bool init_shader_with_name(shader_t *shader, const char *name)
{
    printf("Initializing shader: %s\n", name);
    uint16_t gl_version = get_gl_version();

    static char master_shader_code[0x1001] = {0,};
    static char shader_code[0x10001] = {0,};
    static char final_shader_code[0x11002] = {0,};
    static signed long filter_token_location = 0;

    if (!master_shader_code[0]) {
        size_t header_len = 0;

        if (gl_version >= 0x300) {
            char* header = "#version 300 es\n#define VERSION 0x300\n";
            header_len = strlen(header);

            strcpy(master_shader_code, header);
        }
        else {
            char* header = "#version 100\n#define VERSION 0x100\n";
            header_len = strlen(header);

            strcpy(master_shader_code, header);
        }

        FILE *master_shader_f = fopen(resource_path("Shaders/WasmMasterShader.fsh"), "r");
        if (!master_shader_f) return false;
        fread(master_shader_code + header_len, 1, sizeof(master_shader_code) - 1, master_shader_f);
        fclose(master_shader_f);
        filter_token_location = strstr(master_shader_code, "{filter}") - master_shader_code;

        if (filter_token_location < 0) {
            master_shader_code[0] = 0;
            return false;
        }
    }

    char shader_path[1024];
    sprintf(shader_path, "Shaders/%s.fsh", name);

    FILE *shader_f = fopen(resource_path(shader_path), "r");
    if (!shader_f) return false;
    memset(shader_code, 0, sizeof(shader_code));
    fread(shader_code, 1, sizeof(shader_code) - 1, shader_f);
    fclose(shader_f);

    memset(final_shader_code, 0, sizeof(final_shader_code));
    memcpy(final_shader_code, master_shader_code, filter_token_location);
    strcpy(final_shader_code + filter_token_location, shader_code);
    strcat(final_shader_code + filter_token_location,
           master_shader_code + filter_token_location + sizeof("{filter}") - 1);

    if (gl_version >= 0x300) {
        shader->program = create_program(vertex_shader_300, final_shader_code);
    }
    else {
        shader->program = create_program(vertex_shader_100, final_shader_code);
    }

    if (shader->program == 0) {
        return false;
    }

    // Attributes
    shader->position_attribute = glGetAttribLocation(shader->program, "aPosition");
    // Uniforms
    if (gl_version < 0x300) {
        shader->input_resolution_uniform = glGetUniformLocation(shader->program, "input_resolution");
    }

    shader->resolution_uniform = glGetUniformLocation(shader->program, "output_resolution");
    shader->origin_uniform = glGetUniformLocation(shader->program, "origin");

    glGenTextures(1, &shader->texture);
    glBindTexture(GL_TEXTURE_2D, shader->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    shader->texture_uniform = glGetUniformLocation(shader->program, "image");

    glGenTextures(1, &shader->previous_texture);
    glBindTexture(GL_TEXTURE_2D, shader->previous_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    shader->previous_texture_uniform = glGetUniformLocation(shader->program, "previous_image");

    shader->blending_mode_uniform = glGetUniformLocation(shader->program, "frame_blending_mode");

    // Program

    glUseProgram(shader->program);

    GLuint vao;
    if (gl_version >= 0x300) {
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
    }

    GLuint vbo;
    glGenBuffers(1, &vbo);

    // Attributes

    static GLfloat const quad[16] = {
        -1.f, -1.f, 0, 1,
        -1.f, +1.f, 0, 1,
        +1.f, -1.f, 0, 1,
        +1.f, +1.f, 0, 1,
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(shader->position_attribute);
    glVertexAttribPointer(shader->position_attribute, 4, GL_FLOAT, GL_FALSE, 0, 0);

    return true;
}

void render_bitmap_with_shader(shader_t *shader, void *bitmap, void *previous,
                               unsigned source_width, unsigned source_height,
                               unsigned x, unsigned y, unsigned w, unsigned h,
                               GB_frame_blending_mode_t blending_mode)
{
    glUseProgram(shader->program);
    glUniform2f(shader->origin_uniform, x, y);
    glUniform2f(shader->resolution_uniform, w, h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shader->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source_width, source_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, bitmap);
    glUniform1i(shader->texture_uniform, 0);
    glUniform1i(shader->blending_mode_uniform, previous? blending_mode : GB_FRAME_BLENDING_MODE_DISABLED);
    if (previous) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shader->previous_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source_width, source_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, previous);
        glUniform1i(shader->previous_texture_uniform, 1);
    }
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void free_shader(shader_t *shader)
{
    printf("Freeing shader\n");

    glDeleteProgram(shader->program);
    glDeleteTextures(1, &shader->texture);
    glDeleteTextures(1, &shader->previous_texture);
}
