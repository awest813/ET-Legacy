/* Exercise the real shim allocation/deletion paths without a browser context.
 * emcc misc/web/test_resources.c -Isrc/webgl -O2 -sENVIRONMENT=node
 *      -sWASM_ASYNC_COMPILATION=0 -o build_wasm/test_resources.cjs
 */
#include "../../src/webgl/webgl_shim.c"
#include <assert.h>

static GLuint nextHandle = 1;
static int deletedTextures, deletedShaders, deletedPrograms;
static GLenum backendError;
void emscripten_glGenTextures(GLsizei n, GLuint *names)
{
    for (int i = 0; i < n; i++) names[i] = nextHandle++;
}
void emscripten_glDeleteTextures(GLsizei n, const GLuint *names)
{
    for (int i = 0; i < n; i++) if (names[i]) deletedTextures++;
}
GLuint emscripten_glCreateShader(GLenum type) { return nextHandle++; }
GLuint emscripten_glCreateProgram(void) { return nextHandle++; }
void emscripten_glDeleteShader(GLuint handle) { deletedShaders++; }
void emscripten_glDeleteProgram(GLuint handle) { deletedPrograms++; }
void emscripten_glUseProgram(GLuint handle) {}
GLenum emscripten_glGetError(void) { GLenum error=backendError; backendError=GL_NO_ERROR; return error; }
void emscripten_glEnable(GLenum cap) {}
void emscripten_glDisable(GLenum cap) {}
void emscripten_glActiveTexture(GLenum texture) {}
GLboolean emscripten_glIsEnabled(GLenum cap) { assert(cap == GL_DEPTH_TEST); return GL_TRUE; }
void emscripten_glGetIntegerv(GLenum pname, GLint *value) { *value = pname == GL_MAX_SAMPLES ? 2 : 4096; }
void emscripten_glGetFloatv(GLenum pname, GLfloat *value) { *value = 8.0f; }

int main(void)
{
    lastError=GL_INVALID_ENUM;backendError=GL_INVALID_OPERATION;
    assert(glGetError()==GL_INVALID_ENUM);
    assert(glGetError()==GL_INVALID_OPERATION);
    assert(glGetError()==GL_NO_ERROR);
    GLuint names[SHIM_MAX_TEXTURES - 1], extra;
    for (int cycle = 0; cycle < 3; cycle++)
    {
        glGenTextures(SHIM_MAX_TEXTURES - 1, names);
        for (int i = 0; i < SHIM_MAX_TEXTURES - 1; i++)
            assert(names[i] > 0 && names[i] < SHIM_MAX_TEXTURES && texNameToGL[names[i]]);
        glGenTextures(1, &extra);
        assert(extra == 0 && glGetError() == GL_OUT_OF_MEMORY);
        texUnits[0].bound = names[0];
        texLuminanceMode[names[0]] = 2;
        glDeleteTextures(SHIM_MAX_TEXTURES - 1, names);
        assert(texUnits[0].bound == 0);
        for (int i = 1; i < SHIM_MAX_TEXTURES; i++) assert(!texNameToGL[i] && !texLuminanceMode[i]);
    }
    assert(deletedTextures == 3 * (SHIM_MAX_TEXTURES - 1));
    for (int cycle = 0; cycle < 100; cycle++)
    {
        GLhandleARB shader = glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
        GLhandleARB program = glCreateProgramObjectARB();
        assert(shader && program && shader != program);
        assert(arbShader(shader) && !arbProgram(shader));
        assert(arbProgram(program) && !arbShader(program));
        glDeleteObjectARB(shader);
        glDeleteObjectARB(program);
        assert(!arbShader(shader) && !arbProgram(program));
    }
    assert(deletedShaders == 100 && deletedPrograms == 100);
    st.alphaTest = 1;
    for (int comparison = 0; comparison < 8; comparison++)
    {
        glAlphaFunc(GL_NEVER + comparison, 0.5f);
        assert(alphaTestMode() == comparison + 1);
    }
    glAlphaFunc(GL_GREATER, 2.0f);
    assert(st.alphaRef == 1.0f);
    glAlphaFunc(0xffff, 0.0f);
    assert(glGetError() == GL_INVALID_ENUM && st.alphaFunc == GL_GREATER);
    glDisable(GL_ALPHA_TEST);
    assert(alphaTestMode() == 0);
    for (int capIndex=0;capIndex<4;capIndex++) {
        GLenum cap=(GLenum[]){GL_ALPHA_TEST,GL_FOG,GL_CLIP_PLANE0,GL_TEXTURE_2D}[capIndex];
        glEnable(cap); assert(glIsEnabled(cap)); glDisable(cap); assert(!glIsEnabled(cap));
    }
    glActiveTextureARB(GL_TEXTURE1_ARB);glEnable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);assert(!glIsEnabled(GL_TEXTURE_2D));
    glActiveTextureARB(GL_TEXTURE1_ARB);assert(glIsEnabled(GL_TEXTURE_2D));glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    for (int capIndex=0;capIndex<3;capIndex++) {
        GLenum cap=(GLenum[]){GL_VERTEX_ARRAY,GL_COLOR_ARRAY,GL_TEXTURE_COORD_ARRAY}[capIndex];
        glEnableClientState(cap); assert(glIsEnabled(cap)); glDisableClientState(cap); assert(!glIsEnabled(cap));
    }
    glClientActiveTextureARB(GL_TEXTURE1_ARB);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTextureARB(GL_TEXTURE0_ARB);assert(!glIsEnabled(GL_TEXTURE_COORD_ARRAY));
    glClientActiveTextureARB(GL_TEXTURE1_ARB);assert(glIsEnabled(GL_TEXTURE_COORD_ARRAY));glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTextureARB(GL_TEXTURE0_ARB);assert(glIsEnabled(GL_DEPTH_TEST));
    GLint limit;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit); assert(limit == 4096);
    glGetIntegerv(GL_MAX_SAMPLES, &limit); assert(limit == 2);
    GLfloat anisotropy;
    GLEW_EXT_texture_filter_anisotropic = 0;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy); assert(anisotropy == 1);
    assert(!glewIsSupported("GL_EXT_texture_filter_anisotropic"));
    glGetIntegerv(GL_NUM_EXTENSIONS, &limit); assert(limit == 8);
    assert(!strcmp((const char *)glGetStringi(GL_EXTENSIONS,7), "GL_ARB_depth_texture"));
    GLEW_EXT_texture_filter_anisotropic = 1;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy); assert(anisotropy == 8);
    assert(glewIsSupported("GL_EXT_texture_filter_anisotropic"));
    struct { GLdouble value[4]; GLdouble sentinel[12]; } query;
    for(int i=0;i<12;i++) query.sentinel[i]=12345;
    glColor4f(.2f,.3f,.4f,.5f);
    glGetDoublev(GL_CURRENT_COLOR,query.value);
    assert(fabs(query.value[0]-.2)<.00001 && fabs(query.value[3]-.5)<.00001);
    for(int i=0;i<12;i++) assert(query.sentinel[i]==12345);
    glGetDoublev(GL_ALPHA_TEST_REF,query.value);
    assert(query.value[0]==1 && fabs(query.value[1]-.3)<.00001);
    glEnable(GL_CLIP_PLANE0);
    assert(clip0Enabled);
    glDisable(GL_CLIP_PLANE0);
    assert(!clip0Enabled);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glTranslatef(.3f,-.2f,.5f); glRotatef(67,0,0,1); glScalef(2,3,.5f);
    const GLdouble plane[4]={1,2,-1,-.25};
    glClipPlane(GL_CLIP_PLANE0,plane);
    const GLfloat points[][4]={{0,0,0,1},{.2f,.3f,-.4f,1},{-1,.5f,.25f,1},{.25f,0,0,1}};
    for (int point=0;point<4;point++) {
        GLfloat eye[4]={0}, originalDistance=0, eyeDistance=0;
        for (int row=0;row<4;row++) {
            for (int col=0;col<4;col++) eye[row]+=modelviewStack[modelviewDepth].m[col*4+row]*points[point][col];
            originalDistance+=(GLfloat)plane[row]*points[point][row];
        }
        for (int row=0;row<4;row++) eyeDistance+=clip0Plane[row]*eye[row];
        assert(fabsf(originalDistance-eyeDistance)<.00001f);
    }
    puts("WebGL resources: 24,573 texture allocations and 100 shader/program lifecycles passed.");
    return 0;
}
