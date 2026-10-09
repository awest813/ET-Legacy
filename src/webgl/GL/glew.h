/*
 * ET: Legacy WebAssembly build - GLEW replacement.
 *
 * ET: Legacy's renderer1 includes <GL/glew.h> and calls GL 1.1
 * fixed-function API plus a handful of extensions directly through
 * GLEW. WebGL exposes none of that, so this header declares the exact
 * same surface but backed by src/webgl/webgl_shim.c, which emulates
 * fixed-function OpenGL over WebGL2.
 *
 * Every function here is a real function (GLEW normally exposes
 * extension entry points as function pointers - the engine's
 * `if (glLockArraysEXT)` guards remain valid, they just always pass).
 */
#ifndef __ETWEB_GLEW_H__
#define __ETWEB_GLEW_H__

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__EMSCRIPTEN__)
#error "This glew.h shim is only for the Emscripten/WebGL2 build"
#endif

// ---------------------------------------------------------------------------
// Types (must match Emscripten's khronos platform headers, which GLES3/gl3.h
// includes inside webgl_shim.c)
// ---------------------------------------------------------------------------
typedef unsigned int    GLenum;
typedef unsigned char   GLboolean;
typedef unsigned int    GLbitfield;
typedef void            GLvoid;
typedef signed char     GLbyte;
typedef short           GLshort;
typedef int             GLint;
typedef unsigned char   GLubyte;
typedef unsigned short  GLushort;
typedef unsigned int    GLuint;
typedef int             GLsizei;
typedef float           GLfloat;
typedef float           GLclampf;
typedef double          GLdouble;
typedef double          GLclampd;
typedef char            GLchar;
typedef char            GLcharARB;
typedef unsigned int    GLhandleARB;
typedef intptr_t        GLintptr;
typedef long            GLsizeiptr;   // matches khronos_ssize_t on wasm32

#define GLvoid_cast(x) ((GLvoid *)(x))

// ---------------------------------------------------------------------------
// GLEW status codes / flags
// ---------------------------------------------------------------------------
#define GLEW_OK                        0
#define GLEW_ERROR_NO_GLX_DISPLAY      1

extern int GLEW_ARB_multitexture;
extern int GLEW_ARB_texture_compression;
extern int GLEW_ARB_texture_non_power_of_two;
extern int GLEW_ARB_fragment_program;
extern int GLEW_ARB_framebuffer_object;
extern int GLEW_ARB_depth_texture;
extern int GLEW_EXT_compiled_vertex_array;
extern int GLEW_EXT_texture_env_add;
extern int GLEW_EXT_texture_compression_s3tc;
extern int GLEW_EXT_texture_filter_anisotropic;
extern int GLEW_EXT_framebuffer_multisample;
extern int GLEW_S3_s3tc;

const GLubyte *glewGetString(GLenum name);
const GLubyte *glewGetErrorString(GLenum error);
GLenum         glewInit(void);
GLboolean      glewIsSupported(const char *name);

#define GLEW_VERSION 1

// ---------------------------------------------------------------------------
// Core enums (GL 1.1 subset used by the renderer + margins)
// ---------------------------------------------------------------------------
#define GL_FALSE                          0x0000
#define GL_TRUE                           0x0001
#define GL_BYTE                           0x1400
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_SHORT                          0x1402
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_INT                            0x1404
#define GL_UNSIGNED_INT                   0x1405
#define GL_FLOAT                          0x1406
#define GL_2_BYTES                        0x1407
#define GL_3_BYTES                        0x1408
#define GL_4_BYTES                        0x1409
#define GL_DOUBLE                         0x140A

#define GL_POINTS                         0x0000
#define GL_LINES                          0x0001
#define GL_LINE_LOOP                      0x0002
#define GL_LINE_STRIP                     0x0003
#define GL_TRIANGLES                      0x0004
#define GL_TRIANGLE_STRIP                 0x0005
#define GL_TRIANGLE_FAN                   0x0006
#define GL_QUADS                          0x0007
#define GL_QUAD_STRIP                     0x0008
#define GL_POLYGON                        0x0009

#define GL_NEVER                          0x0200
#define GL_LESS                           0x0201
#define GL_EQUAL                          0x0202
#define GL_LEQUAL                         0x0203
#define GL_GREATER                        0x0204
#define GL_NOTEQUAL                       0x0205
#define GL_GEQUAL                         0x0206
#define GL_ALWAYS                         0x0207

#define GL_KEEP                           0x1E00
#define GL_REPLACE                        0x1E01
#define GL_INCR                           0x1E02
#define GL_DECR                           0x1E03
#define GL_INVERT                         0x150A
#define GL_INCR_WRAP                      0x8507
#define GL_DECR_WRAP                      0x8508

#define GL_FLAT                           0x1D00
#define GL_SMOOTH                         0x1D01

#define GL_VENDOR                         0x1F00
#define GL_RENDERER                       0x1F01
#define GL_VERSION                        0x1F02
#define GL_EXTENSIONS                     0x1F03

#define GL_GENERATE_MIPMAP_SGIS           0x8191
#define GL_GENERATE_MIPMAP_HINT_SGIS      0x8192
#define GL_TEXTURE_MIN_LOD_SGIS           0x813A
#define GL_TEXTURE_MAX_LOD_SGIS           0x813B

#define GL_ZERO                           0x0000
#define GL_ONE                            0x0001
#define GL_SRC_COLOR                      0x0300
#define GL_ONE_MINUS_SRC_COLOR            0x0301
#define GL_SRC_ALPHA                      0x0302
#define GL_ONE_MINUS_SRC_ALPHA            0x0303
#define GL_DST_ALPHA                      0x0304
#define GL_ONE_MINUS_DST_ALPHA            0x0305
#define GL_DST_COLOR                      0x0306
#define GL_ONE_MINUS_DST_COLOR            0x0307
#define GL_SRC_ALPHA_SATURATE             0x0308

#define GL_FRONT                          0x0404
#define GL_BACK                           0x0405
#define GL_FRONT_AND_BACK                 0x0408

#define GL_CW                             0x0900
#define GL_CCW                            0x0901

#define GL_INVALID_ENUM                   0x0500
#define GL_INVALID_VALUE                  0x0501
#define GL_INVALID_OPERATION              0x0502
#define GL_STACK_OVERFLOW                 0x0503
#define GL_STACK_UNDERFLOW                0x0504
#define GL_OUT_OF_MEMORY                  0x0505
#define GL_INVALID_FRAMEBUFFER_OPERATION  0x0506
#define GL_NO_ERROR                       0

#define GL_DEPTH_BUFFER_BIT               0x00000100
#define GL_STENCIL_BUFFER_BIT             0x00000400
#define GL_COLOR_BUFFER_BIT               0x00004000
#define GL_COLOR_BUFFER_BIT0              0x00000001
#define GL_COLOR_BUFFER_BIT1              0x00000002
#define GL_COLOR_BUFFER_BIT2              0x00000004
#define GL_COLOR_BUFFER_BIT3              0x00000008
#define GL_COLOR_BUFFER_BIT4              0x00000010
#define GL_COLOR_BUFFER_BIT5              0x00000020
#define GL_COLOR_BUFFER_BIT6              0x00000040
#define GL_COLOR_BUFFER_BIT7              0x00000080

#define GL_CURRENT_COLOR                  0x0B00
#define GL_CURRENT_INDEX                  0x0B01
#define GL_CURRENT_NORMAL                 0x0B02
#define GL_CURRENT_TEXTURE_COORDS         0x0B03

#define GL_MODELVIEW                      0x1700
#define GL_PROJECTION                     0x1701
#define GL_TEXTURE                        0x1702

#define GL_MATRIX_MODE                    0x0BA0
#define GL_MODELVIEW_MATRIX               0x0BA6
#define GL_PROJECTION_MATRIX              0x0BA7
#define GL_TEXTURE_MATRIX                 0x0BA8
#define GL_MODELVIEW_STACK_DEPTH          0x0BA3
#define GL_PROJECTION_STACK_DEPTH         0x0BA4
#define GL_TEXTURE_STACK_DEPTH            0x0BA5
#define GL_MAX_MODELVIEW_STACK_DEPTH      0x0D36
#define GL_MAX_PROJECTION_STACK_DEPTH     0x0D38
#define GL_MAX_TEXTURE_STACK_DEPTH        0x0D39
#define GL_NORMALIZE                      0x0BA6

#define GL_ALPHA_TEST                     0x0BC0
#define GL_ALPHA_TEST_FUNC                0x0BC1
#define GL_ALPHA_TEST_REF                 0x0BC2
#define GL_BLEND                          0x0BE2
#define GL_BLEND_SRC                      0x0BE1
#define GL_BLEND_DST                      0x0BE0
#define GL_LOGIC_OP_MODE                  0x0BF0
#define GL_DRAW_BUFFER                    0x0C01
#define GL_READ_BUFFER                    0x0C02
#define GL_SCISSOR_BOX                    0x0C10
#define GL_SCISSOR_TEST                   0x0C11
#define GL_COLOR_CLEAR_VALUE              0x0C22
#define GL_COLOR_WRITEMASK                0x0C23
#define GL_DOUBLEBUFFER                   0x0C32
#define GL_STEREO                         0x0C33
#define GL_LINE_SMOOTH                    0x0B20
#define GL_LINE_SMOOTH_HINT               0x0C52
#define GL_LINE_WIDTH                     0x0B21
#define GL_LINE_WIDTH_GRANULARITY         0x0B23
#define GL_LINE_WIDTH_RANGE               0x0B22
#define GL_POINT_SMOOTH                   0x0B10
#define GL_POINT_SMOOTH_HINT              0x0C51
#define GL_POINT_SIZE                     0x0B11
#define GL_POINT_SIZE_GRANULARITY         0x0B13
#define GL_POINT_SIZE_RANGE               0x0B12
#define GL_POLYGON_MODE                   0x0B40
#define GL_POLYGON_SMOOTH                 0x0B41
#define GL_POLYGON_SMOOTH_HINT            0x0C53
#define GL_POLYGON_OFFSET_FILL            0x8037
#define GL_POLYGON_OFFSET_FACTOR          0x8038
#define GL_POLYGON_OFFSET_UNITS           0x2A00
#define GL_POLYGON_OFFSET_POINT           0x2A01
#define GL_POLYGON_OFFSET_LINE            0x2A02
#define GL_CULL_FACE                      0x0B44
#define GL_CULL_FACE_MODE                 0x0B45
#define GL_FRONT_FACE                     0x0B46
#define GL_DEPTH_TEST                     0x0B71
#define GL_DEPTH_CLEAR_VALUE              0x0B73
#define GL_DEPTH_FUNC                     0x0B74
#define GL_DEPTH_RANGE                    0x0B70
#define GL_DEPTH_WRITEMASK                0x0B72
#define GL_STENCIL_TEST                   0x0B90
#define GL_STENCIL_CLEAR_VALUE            0x0B91
#define GL_STENCIL_FUNC                   0x0B92
#define GL_STENCIL_VALUE_MASK             0x0B93
#define GL_STENCIL_FAIL                   0x0B94
#define GL_STENCIL_PASS_DEPTH_FAIL        0x0B95
#define GL_STENCIL_PASS_DEPTH_PASS        0x0B96
#define GL_STENCIL_REF                    0x0B97
#define GL_STENCIL_WRITEMASK              0x0B98
#define GL_VIEWPORT                       0x0BA2
#define GL_DITHER                         0x0BD0
#define GL_FOG                            0x0B60
#define GL_FOG_COLOR                      0x0B66
#define GL_FOG_DENSITY                    0x0B62
#define GL_FOG_END                        0x0B64
#define GL_FOG_HINT                       0x0C54
#define GL_FOG_INDEX                      0x0B61
#define GL_FOG_MODE                       0x0B65
#define GL_FOG_START                      0x0B63
#define GL_EXP                            0x0800
#define GL_EXP2                           0x0801
#define GL_LINEAR                         0x2601

#define GL_DONT_CARE                      0x1100
#define GL_FASTEST                        0x1101
#define GL_NICEST                         0x1102
#define GL_PERSPECTIVE_CORRECTION_HINT    0x0C50

#define GL_LIGHTING                       0x0B50
#define GL_LIGHT0                         0x4000
#define GL_LIGHT1                         0x4001
#define GL_LIGHT2                         0x4002
#define GL_LIGHT3                         0x4003

#define GL_CLIP_PLANE0                    0x3000
#define GL_CLIP_PLANE1                    0x3001
#define GL_CLIP_PLANE2                    0x3002
#define GL_CLIP_PLANE3                    0x3003
#define GL_CLIP_PLANE4                    0x3004
#define GL_CLIP_PLANE5                    0x3005

#define GL_COLOR_INDEX                    0x1900
#define GL_STENCIL_INDEX                  0x1901
#define GL_DEPTH_COMPONENT                0x1902
#define GL_RED                            0x1903
#define GL_GREEN                          0x1904
#define GL_BLUE                           0x1905
#define GL_ALPHA                          0x1906
#define GL_RGB                            0x1907
#define GL_RGBA                           0x1908
#define GL_LUMINANCE                      0x1909
#define GL_LUMINANCE_ALPHA                0x190A
#define GL_BITMAP                         0x0A00

#define GL_POINT                          0x1B00
#define GL_LINE                           0x1B01
#define GL_FILL                           0x1B02

#define GL_RENDER                         0x1C00
#define GL_FEEDBACK                       0x1C01

#define GL_COMPILE                        0x1300
#define GL_COMPILE_AND_EXECUTE            0x1301

#define GL_NEAREST                        0x2600
#define GL_REPEAT                         0x2901
#define GL_CLAMP                          0x2900
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_CLAMP_TO_BORDER                0x812D

#define GL_NEAREST_MIPMAP_NEAREST         0x2700
#define GL_LINEAR_MIPMAP_NEAREST          0x2701
#define GL_NEAREST_MIPMAP_LINEAR          0x2702
#define GL_LINEAR_MIPMAP_LINEAR           0x2703

#define GL_TEXTURE_ENV                    0x2300
#define GL_TEXTURE_ENV_MODE               0x2200
#define GL_TEXTURE_1D                     0x0DE0
#define GL_TEXTURE_2D                     0x0DE1
#define GL_TEXTURE_WRAP_S                 0x2802
#define GL_TEXTURE_WRAP_T                 0x2803
#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_MIN_FILTER             0x2801
#define GL_TEXTURE_BORDER_COLOR           0x2804
#define GL_TEXTURE_WIDTH                  0x1000
#define GL_TEXTURE_HEIGHT                 0x1001
#define GL_TEXTURE_BORDER                 0x1005
#define GL_TEXTURE_COMPONENTS             0x1003
#define GL_TEXTURE_RED_SIZE               0x805C
#define GL_TEXTURE_GREEN_SIZE             0x805D
#define GL_TEXTURE_BLUE_SIZE              0x805E
#define GL_TEXTURE_ALPHA_SIZE             0x805F
#define GL_TEXTURE_LUMINANCE_SIZE         0x8060
#define GL_TEXTURE_INTENSITY_SIZE         0x8061
#define GL_TEXTURE_PRIORITY               0x8066
#define GL_TEXTURE_RESIDENT               0x8067
#define GL_TEXTURE_BINDING_1D             0x8068
#define GL_TEXTURE_BINDING_2D             0x8069
#define GL_PROXY_TEXTURE_1D               0x8063
#define GL_PROXY_TEXTURE_2D               0x8064

#define GL_MODULATE                       0x2100
#define GL_DECAL                          0x2101
#define GL_ADD                            0x0104
#define GL_REPLACE                        0x1E01

#define GL_R3_G3_B2                       0x2A10
#define GL_ALPHA4                         0x803B
#define GL_ALPHA8                         0x803C
#define GL_ALPHA12                        0x803D
#define GL_ALPHA16                        0x803E
#define GL_LUMINANCE4                     0x803F
#define GL_LUMINANCE8                     0x8040
#define GL_LUMINANCE12                    0x8041
#define GL_LUMINANCE16                    0x8042
#define GL_LUMINANCE4_ALPHA4              0x8043
#define GL_LUMINANCE6_ALPHA2              0x8044
#define GL_LUMINANCE8_ALPHA8              0x8045
#define GL_LUMINANCE12_ALPHA4             0x8046
#define GL_LUMINANCE12_ALPHA12            0x8047
#define GL_LUMINANCE16_ALPHA16            0x8048
#define GL_INTENSITY                      0x8049
#define GL_INTENSITY4                     0x804A
#define GL_INTENSITY8                     0x804B
#define GL_INTENSITY12                    0x804C
#define GL_INTENSITY16                    0x804D
#define GL_RGB4                           0x804F
#define GL_RGB5                           0x8050
#define GL_RGB8                           0x8051
#define GL_RGB10                          0x8052
#define GL_RGB12                          0x8053
#define GL_RGB16                          0x8054
#define GL_RGBA2                          0x8055
#define GL_RGBA4                          0x8056
#define GL_RGB5_A1                        0x8057
#define GL_RGBA8                          0x8058
#define GL_RGB10_A2                       0x8059
#define GL_RGBA12                         0x805A
#define GL_RGBA16                         0x805B

#define GL_UNSIGNED_SHORT_4_4_4_4         0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1         0x8034
#define GL_UNSIGNED_SHORT_5_6_5           0x8363
#define GL_UNSIGNED_INT_8_8_8_8           0x8035

#define GL_VERTEX_ARRAY                   0x8074
#define GL_NORMAL_ARRAY                   0x8075
#define GL_COLOR_ARRAY                    0x8076
#define GL_INDEX_ARRAY                    0x8077
#define GL_TEXTURE_COORD_ARRAY            0x8078
#define GL_EDGE_FLAG_ARRAY                0x8079
#define GL_VERTEX_ARRAY_SIZE              0x807A
#define GL_VERTEX_ARRAY_TYPE              0x807B
#define GL_VERTEX_ARRAY_STRIDE            0x807C
#define GL_NORMAL_ARRAY_TYPE              0x807E
#define GL_NORMAL_ARRAY_STRIDE            0x807F
#define GL_COLOR_ARRAY_SIZE               0x8081
#define GL_COLOR_ARRAY_TYPE               0x8082
#define GL_COLOR_ARRAY_STRIDE             0x8083
#define GL_TEXTURE_COORD_ARRAY_SIZE       0x8088
#define GL_TEXTURE_COORD_ARRAY_TYPE       0x8089
#define GL_TEXTURE_COORD_ARRAY_STRIDE     0x808A
#define GL_VERTEX_ARRAY_POINTER           0x808E
#define GL_NORMAL_ARRAY_POINTER           0x808F
#define GL_COLOR_ARRAY_POINTER            0x8090
#define GL_TEXTURE_COORD_ARRAY_POINTER    0x8091
#define GL_VERTEX_ARRAY_COUNT_EXT         0x807D
#define GL_COLOR_ARRAY_COUNT_EXT          0x8084
#define GL_TEXTURE_COORD_ARRAY_COUNT_EXT  0x808B

#define GL_TEXTURE0_ARB                   0x84C0
#define GL_TEXTURE1_ARB                   0x84C1
#define GL_TEXTURE2_ARB                   0x84C2
#define GL_TEXTURE3_ARB                   0x84C3
#define GL_TEXTURE4_ARB                   0x84C4
#define GL_TEXTURE5_ARB                   0x84C5
#define GL_TEXTURE6_ARB                   0x84C6
#define GL_TEXTURE7_ARB                   0x84C7
#define GL_TEXTURE8_ARB                   0x84C8
#define GL_TEXTURE9_ARB                   0x84C9
#define GL_TEXTURE10_ARB                  0x84CA
#define GL_TEXTURE11_ARB                  0x84CB
#define GL_TEXTURE12_ARB                  0x84CC
#define GL_TEXTURE13_ARB                  0x84CD
#define GL_TEXTURE14_ARB                  0x84CE
#define GL_TEXTURE15_ARB                  0x84CF
#define GL_TEXTURE16_ARB                  0x84D0
#define GL_TEXTURE17_ARB                  0x84D1
#define GL_TEXTURE18_ARB                  0x84D2
#define GL_TEXTURE19_ARB                  0x84D3
#define GL_TEXTURE20_ARB                  0x84D4
#define GL_TEXTURE21_ARB                  0x84D5
#define GL_TEXTURE22_ARB                  0x84D6
#define GL_TEXTURE23_ARB                  0x84D7
#define GL_TEXTURE24_ARB                  0x84D8
#define GL_TEXTURE25_ARB                  0x84D9
#define GL_TEXTURE26_ARB                  0x84DA
#define GL_TEXTURE27_ARB                  0x84DB
#define GL_TEXTURE28_ARB                  0x84DC
#define GL_TEXTURE29_ARB                  0x84DD
#define GL_TEXTURE30_ARB                  0x84DE
#define GL_TEXTURE31_ARB                  0x84DF
#define GL_ACTIVE_TEXTURE_ARB             0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE_ARB      0x84E1
#define GL_MAX_TEXTURE_UNITS_ARB          0x84E2
#define GL_MAX_TEXTURE_UNITS              GL_MAX_TEXTURE_UNITS_ARB
#define GL_ACTIVE_TEXTURE                 GL_ACTIVE_TEXTURE_ARB
#define GL_CLIENT_ACTIVE_TEXTURE          GL_CLIENT_ACTIVE_TEXTURE_ARB
#define GL_MAX_TEXTURE_IMAGE_UNITS        0x8872
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D

#define GL_MAX_TEXTURE_SIZE               0x0D33
#define GL_NUM_COMPRESSED_TEXTURE_FORMATS 0x86A2
#define GL_COMPRESSED_TEXTURE_FORMATS     0x86A3
#define GL_TEXTURE_COMPRESSION_HINT       0x84EF
#define GL_TEXTURE_COMPRESSED_IMAGE_SIZE  0x86A0
#define GL_TEXTURE_COMPRESSED             0x86A1

#define GL_RGB_S3TC                       0x83A0
#define GL_RGB4_S3TC                      0x83A1
#define GL_RGBA_S3TC                      0x83A2
#define GL_RGBA4_S3TC                     0x83A3
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT   0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT  0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT  0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT  0x83F3

#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF

#define GL_UNPACK_ALIGNMENT               0x0CF5
#define GL_UNPACK_ROW_LENGTH              0x0CF2
#define GL_UNPACK_SKIP_ROWS               0x0CF3
#define GL_UNPACK_SKIP_PIXELS             0x0CF4
#define GL_UNPACK_SWAP_BYTES              0x0CF0
#define GL_PACK_ALIGNMENT                 0x0D05
#define GL_PACK_ROW_LENGTH                0x0D02
#define GL_PACK_SKIP_ROWS                 0x0D03
#define GL_PACK_SKIP_PIXELS               0x0D04
#define GL_PACK_SWAP_BYTES                0x0D00

#define GL_STENCIL_INDEX1                 0x8D46
#define GL_STENCIL_INDEX4                 0x8D47
#define GL_STENCIL_INDEX8                 0x8D48
#define GL_STENCIL_INDEX16                0x8D49
#define GL_DEPTH_COMPONENT16              0x81A5
#define GL_DEPTH_COMPONENT24              0x81A6
#define GL_DEPTH_COMPONENT32              0x81A7
#define GL_DEPTH_COMPONENT32F             0x8CAC
#define GL_DEPTH24_STENCIL8               0x88F0
#define GL_DEPTH_STENCIL                  0x84F9
#define GL_UNSIGNED_INT_24_8              0x84FA
#define GL_UNSIGNED_INT_10F_11F_11F_REV   0x8C3B
#define GL_READ_FRAMEBUFFER_BINDING_EXT   0x8CAA
#define GL_DRAW_FRAMEBUFFER_BINDING_EXT   0x8CA9

// Framebuffer objects (ARB_fbo / EXT values are identical)
#define GL_FRAMEBUFFER_EXT                0x8D40
#define GL_FRAMEBUFFER                    GL_FRAMEBUFFER_EXT
#define GL_RENDERBUFFER_EXT               0x8D41
#define GL_RENDERBUFFER                   GL_RENDERBUFFER_EXT
#define GL_COLOR_ATTACHMENT0_EXT          0x8CE0
#define GL_COLOR_ATTACHMENT1_EXT          0x8CE1
#define GL_COLOR_ATTACHMENT2_EXT          0x8CE2
#define GL_COLOR_ATTACHMENT3_EXT          0x8CE3
#define GL_COLOR_ATTACHMENT4_EXT          0x8CE4
#define GL_COLOR_ATTACHMENT5_EXT          0x8CE5
#define GL_COLOR_ATTACHMENT6_EXT          0x8CE6
#define GL_COLOR_ATTACHMENT7_EXT          0x8CE7
#define GL_COLOR_ATTACHMENT0              GL_COLOR_ATTACHMENT0_EXT
#define GL_DEPTH_ATTACHMENT_EXT           0x8D00
#define GL_DEPTH_ATTACHMENT               GL_DEPTH_ATTACHMENT_EXT
#define GL_STENCIL_ATTACHMENT_EXT         0x8D20
#define GL_STENCIL_ATTACHMENT             GL_STENCIL_ATTACHMENT_EXT
#define GL_DEPTH_STENCIL_ATTACHMENT       0x821A
#define GL_FRAMEBUFFER_COMPLETE_EXT       0x8CD5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT          0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT  0x8CD7
#define GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT          0x8CD9
#define GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT             0x8CDA
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT         0x8CDB
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT         0x8CDC
#define GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE_EXT         0x8D56
#define GL_FRAMEBUFFER_UNSUPPORTED_EXT                    0x8CDD
#define GL_FRAMEBUFFER_BINDING_EXT        0x8CA6
#define GL_FRAMEBUFFER_BINDING            GL_FRAMEBUFFER_BINDING_EXT
#define GL_RENDERBUFFER_BINDING_EXT       0x8CA7
#define GL_MAX_COLOR_ATTACHMENTS_EXT      0x8CDF
#define GL_MAX_COLOR_ATTACHMENTS          GL_MAX_COLOR_ATTACHMENTS_EXT
#define GL_MAX_RENDERBUFFER_SIZE_EXT      0x84E8
#define GL_MAX_RENDERBUFFER_SIZE          GL_MAX_RENDERBUFFER_SIZE_EXT
#define GL_MAX_SAMPLES_EXT                0x8D57
#define GL_MAX_SAMPLES                    GL_MAX_SAMPLES_EXT
#define GL_READ_FRAMEBUFFER               0x8CA8
#define GL_DRAW_FRAMEBUFFER               0x8CA9
#define GL_READ_FRAMEBUFFER_EXT           GL_READ_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER_EXT           GL_DRAW_FRAMEBUFFER

// Shaders (ARB shader objects - used by tr_shader_program.c / gamma pass)
#define GL_VERTEX_SHADER_ARB              0x8B31
#define GL_FRAGMENT_SHADER_ARB            0x8B30
#define GL_PROGRAM_OBJECT_ARB             0x8B41
#define GL_OBJECT_TYPE_ARB                0x8B4E
#define GL_OBJECT_SUBTYPE_ARB             0x8B4F
#define GL_OBJECT_COMPILE_STATUS_ARB      0x8B81
#define GL_OBJECT_LINK_STATUS_ARB         0x8B82
#define GL_OBJECT_VALIDATE_STATUS_ARB     0x8B83
#define GL_OBJECT_INFO_LOG_LENGTH_ARB     0x8B84
#define GL_OBJECT_ATTACHED_OBJECTS_ARB    0x8B85
#define GL_OBJECT_ACTIVE_UNIFORMS_ARB     0x8B86
#define GL_OBJECT_ACTIVE_UNIFORM_MAX_LENGTH_ARB 0x8B87
#define GL_OBJECT_SHADER_SOURCE_LENGTH_ARB 0x8B88
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84
#define GL_SHADER_TYPE                    0x8B4F
#define GL_CURRENT_PROGRAM                0x8B8D
#define GL_FLOAT_VEC2                     0x8B50
#define GL_FLOAT_VEC3                     0x8B51
#define GL_FLOAT_VEC4                     0x8B52
#define GL_FLOAT_MAT3                     0x8B5B
#define GL_FLOAT_MAT4                     0x8B5C
#define GL_SAMPLER_2D                     0x8B5E
#define GL_SHADING_LANGUAGE_VERSION       0x8B8C

// Occlusion queries (only queried for bit count - we answer 0 = unsupported)
#define GL_SAMPLES_PASSED_ARB             0x8914
#define GL_SAMPLES_PASSED                 GL_SAMPLES_PASSED_ARB
#define GL_QUERY_COUNTER_BITS_ARB         0x8864
#define GL_QUERY_COUNTER_BITS             GL_QUERY_COUNTER_BITS_ARB
#define GL_QUERY_RESULT_ARB               0x8866
#define GL_CURRENT_QUERY_ARB              0x8865

// ARB_fragment_program (only checked for availability)
#define GL_FRAGMENT_PROGRAM_ARB           0x8804
#define GL_PROGRAM_ERROR_STRING_ARB       0x8874

#define GL_MULTISAMPLE_ARB                0x809D

// ---------------------------------------------------------------------------
// Core GL 1.1 functions (implemented in webgl_shim.c)
// ---------------------------------------------------------------------------

// State / errors
void         glClear(GLbitfield mask);
void         glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void         glClearDepth(GLclampd depth);
void         glClearStencil(GLint s);
GLenum       glGetError(void);
const GLubyte *glGetString(GLenum name);
const GLubyte *glGetStringi(GLenum name, GLuint index);
void         glGetBooleanv(GLenum pname, GLboolean *params);
void         glGetIntegerv(GLenum pname, GLint *params);
void         glGetFloatv(GLenum pname, GLfloat *params);
void         glGetDoublev(GLenum pname, GLdouble *params);
void         glFinish(void);
void         glFlush(void);
void         glHint(GLenum target, GLenum mode);

// Enable/disable
void         glEnable(GLenum cap);
void         glDisable(GLenum cap);
GLboolean    glIsEnabled(GLenum cap);
GLboolean    glIsTexture(GLuint texture);

// Transforms
void         glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void         glMatrixMode(GLenum mode);
void         glLoadIdentity(void);
void         glLoadMatrixf(const GLfloat *m);
void         glLoadMatrixd(const GLdouble *m);
void         glMultMatrixf(const GLfloat *m);
void         glPushMatrix(void);
void         glPopMatrix(void);
void         glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void         glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void         glScalef(GLfloat x, GLfloat y, GLfloat z);
void         glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void         glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void         glDepthRange(GLclampd near_val, GLclampd far_val);

// Raster / fragments
void         glDepthFunc(GLenum func);
void         glDepthMask(GLboolean flag);
void         glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void         glBlendFunc(GLenum sfactor, GLenum dfactor);
void         glAlphaFunc(GLenum func, GLclampf ref);
void         glLogicOp(GLenum opcode);
void         glCullFace(GLenum mode);
void         glFrontFace(GLenum mode);
void         glShadeModel(GLenum mode);
void         glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void         glStencilFunc(GLenum func, GLint ref, GLuint mask);
void         glStencilMask(GLuint mask);
void         glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void         glPolygonMode(GLenum face, GLenum mode);
void         glPolygonOffset(GLfloat factor, GLfloat units);
void         glLineWidth(GLfloat width);
void         glPointSize(GLfloat size);
void         glClipPlane(GLenum plane, const GLdouble *equation);

// Fog
void         glFogf(GLenum pname, GLfloat param);
void         glFogi(GLenum pname, GLint param);
void         glFogfv(GLenum pname, const GLfloat *params);
void         glFogiv(GLenum pname, const GLint *params);

// Drawing
void         glBegin(GLenum mode);
void         glEnd(void);
void         glVertex2f(GLfloat x, GLfloat y);
void         glVertex2fv(const GLfloat *v);
void         glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void         glVertex3fv(const GLfloat *v);
void         glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void         glVertex4fv(const GLfloat *v);
void         glColor3f(GLfloat red, GLfloat green, GLfloat blue);
void         glColor3fv(const GLfloat *v);
void         glColor3ub(GLubyte red, GLubyte green, GLubyte blue);
void         glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void         glColor4fv(const GLfloat *v);
void         glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
void         glColor4ubv(const GLubyte *v);
void         glTexCoord1f(GLfloat s);
void         glTexCoord2f(GLfloat s, GLfloat t);
void         glTexCoord2fv(const GLfloat *v);
void         glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void         glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t);
void         glMultiTexCoord2fvARB(GLenum target, const GLfloat *v);
void         glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
void         glRasterPos3fv(const GLfloat *v);
void         glBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig,
                      GLfloat xmove, GLfloat ymove, const GLubyte *bitmap);
void         glDrawArrays(GLenum mode, GLint first, GLsizei count);
void         glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void         glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count,
                                 GLenum type, const GLvoid *indices);

// Vertex arrays
void         glEnableClientState(GLenum cap);
void         glDisableClientState(GLenum cap);
void         glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void         glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void         glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void         glIndexPointer(GLenum type, GLsizei stride, const GLvoid *ptr);
void         glNormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr);
void         glLockArraysEXT(GLint first, GLsizei count);
void         glUnlockArraysEXT(void);

// Textures
void         glGenTextures(GLsizei n, GLuint *textures);
void         glDeleteTextures(GLsizei n, const GLuint *textures);
void         glBindTexture(GLenum target, GLuint texture);
void         glTexImage2D(GLenum target, GLint level, GLint internalFormat,
                          GLsizei width, GLsizei height, GLint border,
                          GLenum format, GLenum type, const GLvoid *data);
void         glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                             GLsizei width, GLsizei height, GLenum format, GLenum type,
                             const GLvoid *data);
void         glCopyTexImage2D(GLenum target, GLint level, GLenum internalFormat,
                              GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
void         glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                 GLint x, GLint y, GLsizei width, GLsizei height);
void         glTexParameterf(GLenum target, GLenum pname, GLfloat param);
void         glTexParameteri(GLenum target, GLenum pname, GLint param);
void         glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
void         glTexParameteriv(GLenum target, GLenum pname, const GLint *params);
void         glTexEnvf(GLenum target, GLenum pname, GLfloat param);
void         glTexEnvi(GLenum target, GLenum pname, GLint param);
void         glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params);
void         glTexEnviv(GLenum target, GLenum pname, const GLint *params);
void         glPixelStorei(GLenum pname, GLint param);
void         glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities);

// Multitexture (ARB)
void         glActiveTextureARB(GLenum texture);
void         glClientActiveTextureARB(GLenum texture);

// Display lists (engine references are legacy/unused paths - stubs)
GLuint       glGenLists(GLsizei range);
void         glDeleteLists(GLuint list, GLsizei range);
void         glNewList(GLuint list, GLenum mode);
void         glEndList(void);
void         glCallList(GLuint list);
void         glCallLists(GLsizei n, GLenum type, const GLvoid *lists);
void         glListBase(GLuint base);
GLboolean    glIsList(GLuint list);

// Pixels
void         glReadBuffer(GLenum mode);
void         glDrawBuffer(GLenum mode);
void         glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                          GLenum format, GLenum type, GLvoid *data);

// Framebuffer objects
void         glGenFramebuffersEXT(GLsizei n, GLuint *framebuffers);
void         glDeleteFramebuffersEXT(GLsizei n, const GLuint *framebuffers);
void         glBindFramebufferEXT(GLenum target, GLuint framebuffer);
void         glGenRenderbuffersEXT(GLsizei n, GLuint *renderbuffers);
void         glDeleteRenderbuffersEXT(GLsizei n, const GLuint *renderbuffers);
void         glBindRenderbufferEXT(GLenum target, GLuint renderbuffer);
void         glRenderbufferStorageEXT(GLenum target, GLenum internalFormat,
                                      GLsizei width, GLsizei height);
void         glRenderbufferStorageMultisampleEXT(GLenum target, GLsizei samples,
                                                 GLenum internalFormat, GLsizei width, GLsizei height);
void         glFramebufferRenderbufferEXT(GLenum target, GLenum attachment,
                                          GLenum renderbuffertarget, GLuint renderbuffer);
void         glFramebufferTexture2DEXT(GLenum target, GLenum attachment, GLenum textarget,
                                       GLuint texture, GLint level);
void         glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                                    GLuint texture, GLint level);
GLenum       glCheckFramebufferStatusEXT(GLenum target);
void         glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                               GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                               GLbitfield mask, GLenum filter);
void         glBlitFramebufferEXT(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                                  GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                                  GLbitfield mask, GLenum filter);
void         glGenerateMipmapEXT(GLenum target);

// ARB shader objects (gamma postprocess program)
GLhandleARB  glCreateShaderObjectARB(GLenum shaderType);
void         glShaderSourceARB(GLhandleARB shader, GLsizei count, const GLcharARB **string,
                               const GLint *length);
void         glCompileShaderARB(GLhandleARB shader);
GLhandleARB  glCreateProgramObjectARB(void);
void         glAttachObjectARB(GLhandleARB containerObj, GLhandleARB obj);
void         glDetachObjectARB(GLhandleARB containerObj, GLhandleARB attachedObj);
void         glLinkProgramARB(GLhandleARB programObj);
void         glUseProgramObjectARB(GLhandleARB programObj);
void         glDeleteObjectARB(GLhandleARB obj);
void         glGetObjectParameterivARB(GLhandleARB obj, GLenum pname, GLint *params);
void         glGetInfoLogARB(GLhandleARB obj, GLsizei maxLength, GLsizei *length, GLcharARB *infoLog);
GLint        glGetUniformLocation(GLhandleARB programObj, const GLchar *name);
GLint        glGetUniformLocationARB(GLhandleARB programObj, const GLcharARB *name);
void         glUniform1f(GLint location, GLfloat v0);
void         glUniform1i(GLint location, GLint v0);
void         glUniform2f(GLint location, GLfloat v0, GLfloat v1);
void         glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
void         glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
void         glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void         glGetShaderiv(GLhandleARB shader, GLenum pname, GLint *params);

// Occlusion queries (stubbed - answered as unsupported)
void         glGetQueryivARB(GLenum target, GLenum pname, GLint *params);

#ifdef __cplusplus
}
#endif

#endif // __ETWEB_GLEW_H__
