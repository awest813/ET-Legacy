/*
 * ET: Legacy WebAssembly build - fixed-function OpenGL -> WebGL2 emulator.
 *
 * renderer1 is a classic GL 1.1 fixed-function renderer (matrices, immediate
 * mode, client-side vertex arrays, 2-unit multitexture, stencil shadows,
 * FBO/MSAA resolve, one ARB-shader gamma postprocess). WebGL2 has none of
 * the fixed-function pipeline, so this shim implements it:
 *
 *  - matrix stacks (modelview/projection/texture) tracked on the CPU
 *  - one ES3 shader program emulating TexEnv combiners, alpha test,
 *    clip plane and GL fog
 *  - client-side vertex arrays re-packed into an interleaved VBO per draw
 *  - immediate mode (glBegin/glEnd) captured and flushed the same way
 *  - textures, blend, stencil, scissor, FBOs pass through to WebGL2
 *  - ARB shader objects are transpiled from GLSL 110 to GLSL ES 300
 *    (attribute/varying/texture2D/gl_FragColor -> ES3 equivalents)
 *
 * The real GLES3/WebGL2 entry points are aliased with a gl3_ prefix so
 * this file can define the GL 1.1 names itself.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <emscripten/html5_webgl.h>

#include "GL/glew.h"

// ---------------------------------------------------------------------------
// Alias the real WebGL2 implementation.
//
// Emscripten exports every GL entry point twice: under the standard GL name
// (implemented by its JS glue) and under an emscripten_gl* alias. We define
// the GL 1.1 names ourselves, so the shim internally calls the
// emscripten_gl* aliases to reach the real WebGL2 functions.
// ---------------------------------------------------------------------------
#define gl3_Enable              emscripten_glEnable
#define gl3_Disable             emscripten_glDisable
#define gl3_Clear               emscripten_glClear
#define gl3_ClearColor          emscripten_glClearColor
#define gl3_ClearDepthf         emscripten_glClearDepthf
#define gl3_ClearStencil        emscripten_glClearStencil
#define gl3_GetError            emscripten_glGetError
#define gl3_Finish              emscripten_glFinish
#define gl3_Flush               emscripten_glFlush
#define gl3_Viewport            emscripten_glViewport
#define gl3_DepthFunc           emscripten_glDepthFunc
#define gl3_DepthMask           emscripten_glDepthMask
#define gl3_DepthRangef         emscripten_glDepthRangef
#define gl3_ColorMask           emscripten_glColorMask
#define gl3_BlendFunc           emscripten_glBlendFunc
#define gl3_CullFace            emscripten_glCullFace
#define gl3_FrontFace           emscripten_glFrontFace
#define gl3_Scissor             emscripten_glScissor
#define gl3_StencilFunc         emscripten_glStencilFunc
#define gl3_StencilMask         emscripten_glStencilMask
#define gl3_StencilOp           emscripten_glStencilOp
#define gl3_PolygonOffset       emscripten_glPolygonOffset
#define gl3_LineWidth           emscripten_glLineWidth
#define gl3_PointSize           emscripten_glPointSize
#define gl3_Hint                emscripten_glHint
#define gl3_ReadPixels          emscripten_glReadPixels
#define gl3_ReadBuffer          emscripten_glReadBuffer
#define gl3_DrawBuffer          emscripten_glDrawBuffer
#define gl3_DrawArrays          emscripten_glDrawArrays
#define gl3_DrawElements        emscripten_glDrawElements
#define gl3_GenTextures         emscripten_glGenTextures
#define gl3_DeleteTextures      emscripten_glDeleteTextures
#define gl3_BindTexture         emscripten_glBindTexture
#define gl3_TexImage2D          emscripten_glTexImage2D
#define gl3_TexSubImage2D       emscripten_glTexSubImage2D
#define gl3_CopyTexImage2D      emscripten_glCopyTexImage2D
#define gl3_CopyTexSubImage2D   emscripten_glCopyTexSubImage2D
#define gl3_TexParameteri       emscripten_glTexParameteri
#define gl3_TexParameterf       emscripten_glTexParameterf
#define gl3_PixelStorei         emscripten_glPixelStorei
#define gl3_ActiveTexture       emscripten_glActiveTexture
#define gl3_IsTexture           emscripten_glIsTexture
#define gl3_GetBooleanv         emscripten_glGetBooleanv
#define gl3_GetIntegerv         emscripten_glGetIntegerv
#define gl3_GetFloatv           emscripten_glGetFloatv
#define gl3_GetString           emscripten_glGetString
#define gl3_GetStringi          emscripten_glGetStringi
#define gl3_GenBuffers          emscripten_glGenBuffers
#define gl3_DeleteBuffers       emscripten_glDeleteBuffers
#define gl3_BindBuffer          emscripten_glBindBuffer
#define gl3_BufferData          emscripten_glBufferData
#define gl3_BufferSubData       emscripten_glBufferSubData
#define gl3_GenVertexArrays     emscripten_glGenVertexArrays
#define gl3_BindVertexArray     emscripten_glBindVertexArray
#define gl3_CreateShader        emscripten_glCreateShader
#define gl3_DeleteShader        emscripten_glDeleteShader
#define gl3_DeleteProgram       emscripten_glDeleteProgram
#define gl3_ShaderSource        emscripten_glShaderSource
#define gl3_CompileShader       emscripten_glCompileShader
#define gl3_GetShaderiv         emscripten_glGetShaderiv
#define gl3_GetShaderInfoLog    emscripten_glGetShaderInfoLog
#define gl3_CreateProgram       emscripten_glCreateProgram
#define gl3_AttachShader        emscripten_glAttachShader
#define gl3_DetachShader        emscripten_glDetachShader
#define gl3_LinkProgram         emscripten_glLinkProgram
#define gl3_GetProgramiv        emscripten_glGetProgramiv
#define gl3_GetProgramInfoLog   emscripten_glGetProgramInfoLog
#define gl3_UseProgram          emscripten_glUseProgram
#define gl3_BindAttribLocation  emscripten_glBindAttribLocation
#define gl3_GetUniformLocation  emscripten_glGetUniformLocation
#define gl3_Uniform1f           emscripten_glUniform1f
#define gl3_Uniform1i           emscripten_glUniform1i
#define gl3_Uniform2f           emscripten_glUniform2f
#define gl3_Uniform3f           emscripten_glUniform3f
#define gl3_Uniform4f           emscripten_glUniform4f
#define gl3_UniformMatrix4fv    emscripten_glUniformMatrix4fv
#define gl3_GenFramebuffers     emscripten_glGenFramebuffers
#define gl3_DeleteFramebuffers  emscripten_glDeleteFramebuffers
#define gl3_BindFramebuffer     emscripten_glBindFramebuffer
#define gl3_FramebufferTexture2D emscripten_glFramebufferTexture2D
#define gl3_GenRenderbuffers    emscripten_glGenRenderbuffers
#define gl3_DeleteRenderbuffers emscripten_glDeleteRenderbuffers
#define gl3_BindRenderbuffer    emscripten_glBindRenderbuffer
#define gl3_RenderbufferStorage emscripten_glRenderbufferStorage
#define gl3_RenderbufferStorageMultisample emscripten_glRenderbufferStorageMultisample
#define gl3_FramebufferRenderbuffer emscripten_glFramebufferRenderbuffer
#define gl3_CheckFramebufferStatus emscripten_glCheckFramebufferStatus
#define gl3_GetFramebufferAttachmentParameteriv emscripten_glGetFramebufferAttachmentParameteriv
#define gl3_BlitFramebuffer     emscripten_glBlitFramebuffer
#define gl3_GenerateMipmap      emscripten_glGenerateMipmap
#define gl3_VertexAttribPointer emscripten_glVertexAttribPointer
#define gl3_EnableVertexAttribArray emscripten_glEnableVertexAttribArray
#define gl3_DisableVertexAttribArray emscripten_glDisableVertexAttribArray
#define gl3_IsEnabled           emscripten_glIsEnabled

#include <webgl/webgl1.h>
#include <webgl/webgl2.h>   // emscripten_gl* entry points
#include <GLES3/gl3.h>   // GL enums/constants

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
#define SHIM_MAX_TMUS             4
#define SHIM_MATRIX_STACK_DEPTH   16
#define SHIM_ATTRIB_STACK_DEPTH   8
#define SHIM_MAX_IMMEDIATE        262144     // vertices captured between glBegin/glEnd
#define SHIM_SCRATCH_INITIAL      (1 << 18)  // floats

// TexEnv modes
#define TEXENV_REPLACE  0
#define TEXENV_MODULATE 1
#define TEXENV_ADD      2
#define TEXENV_DECAL    3
#define TEXENV_BLEND    4

typedef struct
{
	GLfloat m[16];
} mat4_t;

typedef struct
{
	GLboolean enabled;
	GLuint    bound;        // engine texture name
	GLenum    envMode;      // TEXENV_*
} texUnitState_t;

typedef struct
{
	GLboolean enabled;
	GLint     size;
	GLenum    type;
	GLsizei   stride;
	const void *ptr;
} clientArray_t;

typedef struct
{
	// enables (subset the engine uses)
	int depthTest, depthMask, blend, alphaTest, cullFace, scissorTest, stencilTest, fog, texture2D[SHIM_MAX_TMUS];
	GLenum depthFunc, cullMode, frontFace;
	GLint sfSrc, sfDst;
	GLenum alphaFunc;
	GLfloat alphaRef;
	GLuint stencilMask;
	GLenum stencilFunc;
	GLint stencilRef;
	GLuint stencilValueMask;
	GLenum stencilFail, stencilZFail, stencilZPass;
	GLint scissorX, scissorY;
	GLsizei scissorW, scissorH;
	GLboolean colorMaskR, colorMaskG, colorMaskB, colorMaskA;
	GLfloat polygonOffsetFactor, polygonOffsetUnits;
	GLenum shadeModel;
	GLfloat clearColor[4];
	GLclampd clearDepth;
	GLint clearStencil;
} glStateBits_t;

static glStateBits_t  st;
static glStateBits_t  attribStack[SHIM_ATTRIB_STACK_DEPTH];
static int            attribStackDepth = 0;

// matrices
static mat4_t modelviewStack[SHIM_MATRIX_STACK_DEPTH];
static mat4_t projectionStack[SHIM_MATRIX_STACK_DEPTH];
static mat4_t textureStack[SHIM_MAX_TMUS][4];
static int    modelviewDepth = 0, projectionDepth = 0, textureDepth[SHIM_MAX_TMUS];
static int    curMatrixMode = GL_MODELVIEW;
static int    curTMU = 0;      // active texture unit (glActiveTextureARB)
static int    clientTMU = 0;   // client active texture unit

static texUnitState_t texUnits[SHIM_MAX_TMUS];

// client arrays
static clientArray_t arrVertex;
static clientArray_t arrColor;
static clientArray_t arrTexCoord[SHIM_MAX_TMUS];
static int           clientStateEnabled[8]; // indexed by cap-GL_VERTEX_ARRAY etc.

// current immediate state
static GLfloat curColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static GLfloat curTC[SHIM_MAX_TMUS][4];

// fog
static int    fogEnabled = 0;
static int    fogMode = 0;      // 0 linear, 1 exp, 2 exp2
static GLfloat fogColor[4] = { 0, 0, 0, 1 };
static GLfloat fogStart = 0.0f, fogEnd = 1.0f, fogDensity = 1.0f;

// clip plane 0
static int    clip0Enabled = 0;
static GLfloat clip0Plane[4] = { 0, 0, 1, 0 };

// scratch buffers
static GLfloat *scratchVerts = NULL;   // interleaved: pos3 color4 tc0_2 tc1_2
static size_t   scratchCap = 0;
static size_t   scratchUsed = 0;
static GLuint  *scratchIdx = NULL;
static size_t   idxCap = 0;
static size_t   idxUsed = 0;
static GLuint   vboVerts = 0, vboIdx = 0, vaoMain = 0;
static int      buffersReady = 0;

// FF shader
static GLuint   ffProgram = 0;
static GLint    uMVP = -1, uMV = -1, uColor = -1;
static GLint    uTex0 = -1, uTex1 = -1, uHasTex0 = -1, uHasTex1 = -1;
static GLint    uMode0 = -1, uMode1 = -1;
static GLint    uAlphaFunc = -1, uAlphaRef = -1;
static GLint    uClipEnabled = -1, uClipPlane = -1;
static GLint    uFogEnabled = -1, uFogMode = -1, uFogColor = -1, uFogDensity = -1, uFogStart = -1, uFogEnd = -1;
static int      ffUniformsDirty = 1;

// external (ARB) program
static int      extProgramActive = 0;
static GLuint   extGLProgram = 0;
static GLint    extUMVP = -1, extUMV = -1;

// errors
static GLenum   lastError = GL_NO_ERROR;

// Opt-in draw statistics; keep normal matches free of per-frame console noise.
#ifdef ETWEBGL_DEBUG_STATS
static unsigned long statDraws = 0, statFrames = 0, statVerts = 0;
#endif

static void statDraw(size_t verts)
{
#ifdef ETWEBGL_DEBUG_STATS
	statDraws++;
	statVerts += verts;
	if (statDraws % 2000 == 0)
	{
		fprintf(stderr, "ETWebGL draws: %lu total %lu verts\n", statDraws, statVerts);
	}
#else
	(void)verts;
#endif
}

// viewport
static GLint    vpX = 0, vpY = 0;
static GLsizei  vpW = 320, vpH = 240;
static GLfloat  depthNear = 0.0f, depthFar = 1.0f;

// texture name table (engine names -> identity; WebGL handles come from glGenTextures)
#define SHIM_MAX_TEXTURES 8192
static GLuint   texNameToGL[SHIM_MAX_TEXTURES];
// Desktop luminance storage expands red into RGB when sampled. ET uploads
// RGBA bytes, so preserve that behavior in legal WebGL2 RGBA8 storage.
static GLubyte  texLuminanceMode[SHIM_MAX_TEXTURES]; // 1: opaque, 2: source alpha
static int      texNameCount = 0;   // allocation cursor; deleted names are reusable

// display list stub
static GLuint   listBase = 0;

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------
static void mat4_identity(mat4_t *m)
{
	memset(m->m, 0, sizeof(m->m));
	m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f;
}

static void mat4_mul(mat4_t *out, const mat4_t *a, const mat4_t *b)
{
	mat4_t r;
	int i, j, k;

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			float s = 0.f;
			for (k = 0; k < 4; k++)
			{
				s += a->m[k * 4 + j] * b->m[i * 4 + k];
			}
			r.m[i * 4 + j] = s;
		}
	}
	*out = r;
}

static mat4_t *currentMatrixStack(int *depth, int *maxDepth)
{
	switch (curMatrixMode)
	{
	case GL_MODELVIEW:  *depth = modelviewDepth;  *maxDepth = SHIM_MATRIX_STACK_DEPTH; return modelviewStack;
	case GL_PROJECTION: *depth = projectionDepth; *maxDepth = SHIM_MATRIX_STACK_DEPTH; return projectionStack;
	case GL_TEXTURE:    *depth = textureDepth[curTMU]; *maxDepth = 4; return textureStack[curTMU];
	default:            *depth = 0; *maxDepth = 1; return modelviewStack;
	}
}

static void markUniformsDirty(void)
{
	ffUniformsDirty = 1;
}

static int alphaTestMode(void)
{
	// The shader uses compact comparison codes; GL enums start at 0x0200.
	return st.alphaTest ? (int) st.alphaFunc - GL_NEVER + 1 : 0;
}

static GLenum texenvFromGL(GLenum mode)
{
	switch (mode)
	{
	case GL_REPLACE: return TEXENV_REPLACE;
	case GL_ADD:     return TEXENV_ADD;
	case GL_DECAL:   return TEXENV_DECAL;
	case GL_BLEND:   return TEXENV_BLEND;
	default:         return TEXENV_MODULATE;
	}
}

static size_t attribTypeSize(GLenum type)
{
	switch (type)
	{
	case GL_BYTE:
	case GL_UNSIGNED_BYTE:          return 1;
	case GL_SHORT:
	case GL_UNSIGNED_SHORT:         return 2;
	case GL_INT:
	case GL_UNSIGNED_INT:
	case GL_FLOAT:                  return 4;
	case GL_DOUBLE:                 return 8;
	default:                        return 4;
	}
}

static float fetchComponent(const clientArray_t *a, int index, int comp, GLfloat fallback)
{
	const GLbyte *base;
	size_t tsize, stride;

	if (!a->enabled || !a->ptr || comp >= a->size)
	{
		return fallback;
	}

	tsize  = attribTypeSize(a->type);
	stride = a->stride ? (size_t) a->stride : tsize * (size_t) a->size;
	base   = (const GLbyte *) a->ptr + (size_t) index * stride + (size_t) comp * tsize;

	switch (a->type)
	{
	case GL_FLOAT:          return *(const GLfloat *) base;
	case GL_UNSIGNED_BYTE:  return (float) (*(const GLubyte *) base) / 255.0f;
	case GL_BYTE:           return (float) (*base) / 127.0f;
	case GL_UNSIGNED_SHORT: return (float) (*(const GLushort *) base) / 65535.0f;
	case GL_SHORT:          return (float) (*(const GLshort *) base) / 32767.0f;
	case GL_UNSIGNED_INT:   return (float) (*(const GLuint *) base) / 4294967295.0f;
	case GL_INT:            return (float) (*(const GLint *) base) / 2147483647.0f;
	case GL_DOUBLE:         return (float) (*(const GLdouble *) base);
	default:                return fallback;
	}
}

static void ensureScratch(size_t floats)
{
	if (scratchCap < floats)
	{
		size_t cap = scratchCap ? scratchCap : SHIM_SCRATCH_INITIAL;

		while (cap < floats)
		{
			cap *= 2;
		}
		scratchVerts = (GLfloat *) realloc(scratchVerts, cap * sizeof(GLfloat));
		scratchCap = cap;
	}
}

static void ensureIdx(size_t indices)
{
	if (idxCap < indices)
	{
		size_t cap = idxCap ? idxCap : 4096;

		while (cap < indices)
		{
			cap *= 2;
		}
		scratchIdx = (GLuint *) realloc(scratchIdx, cap * sizeof(GLuint));
		idxCap = cap;
	}
}

// repack the enabled client arrays into the interleaved scratch buffer
static void repackVerts(GLint first, GLsizei count)
{
	size_t need = (size_t) count * 11;

	// renderer1 normally submits float positions/UVs and byte colors. Resolve
	// their types and strides once per draw rather than for every component.
	const int hasColor = arrColor.enabled && arrColor.ptr;
	const int hasTC0 = arrTexCoord[0].enabled && arrTexCoord[0].ptr;
	const int hasTC1 = arrTexCoord[1].enabled && arrTexCoord[1].ptr;

	ensureScratch(need);
	scratchUsed = need;

	if (arrVertex.enabled && arrVertex.ptr && arrVertex.type == GL_FLOAT && arrVertex.size >= 3 &&
	    (!hasColor || (arrColor.type == GL_UNSIGNED_BYTE && arrColor.size == 4)) &&
	    (!hasTC0 || (arrTexCoord[0].type == GL_FLOAT && arrTexCoord[0].size >= 2)) &&
	    (!hasTC1 || (arrTexCoord[1].type == GL_FLOAT && arrTexCoord[1].size >= 2)))
	{
		const size_t vertexStride = arrVertex.stride ? arrVertex.stride : arrVertex.size * sizeof(GLfloat);
		const size_t colorStride = arrColor.stride ? arrColor.stride : arrColor.size;
		const size_t tc0Stride = arrTexCoord[0].stride ? arrTexCoord[0].stride : arrTexCoord[0].size * sizeof(GLfloat);
		const size_t tc1Stride = arrTexCoord[1].stride ? arrTexCoord[1].stride : arrTexCoord[1].size * sizeof(GLfloat);
		for (GLsizei i = 0; i < count; i++)
		{
			const size_t vi = (size_t)first + i;
			const GLfloat *vertex = (const GLfloat *)((const GLubyte *)arrVertex.ptr + vi * vertexStride);
			GLfloat *out = scratchVerts + (size_t)i * 11;
			out[0] = vertex[0]; out[1] = vertex[1]; out[2] = vertex[2];
			if (hasColor)
			{
				const GLubyte *color = (const GLubyte *)arrColor.ptr + vi * colorStride;
				for (int j = 0; j < 4; j++) out[3 + j] = color[j] / 255.0f;
			}
			else
			{
				for (int j = 0; j < 4; j++) out[3 + j] = curColor[j];
			}
			if (hasTC0)
			{
				const GLfloat *tc = (const GLfloat *)((const GLubyte *)arrTexCoord[0].ptr + vi * tc0Stride);
				out[7] = tc[0]; out[8] = tc[1];
			}
			else { out[7] = curTC[0][0]; out[8] = curTC[0][1]; }
			if (hasTC1)
			{
				const GLfloat *tc = (const GLfloat *)((const GLubyte *)arrTexCoord[1].ptr + vi * tc1Stride);
				out[9] = tc[0]; out[10] = tc[1];
			}
			else { out[9] = 0.0f; out[10] = 0.0f; }
		}
		return;
	}

	for (GLsizei i = 0; i < count; i++)
	{
		GLfloat *out = scratchVerts + (size_t) i * 11;
		int vi = first + i;

		out[0] = fetchComponent(&arrVertex, vi, 0, 0.0f);
		out[1] = fetchComponent(&arrVertex, vi, 1, 0.0f);
		out[2] = (arrVertex.enabled && arrVertex.size >= 3) ? fetchComponent(&arrVertex, vi, 2, 0.0f) : 0.0f;

		if (arrColor.enabled && arrColor.ptr)
		{
			out[3] = fetchComponent(&arrColor, vi, 0, 0.0f);
			out[4] = fetchComponent(&arrColor, vi, 1, 0.0f);
			out[5] = fetchComponent(&arrColor, vi, 2, 0.0f);
			out[6] = (arrColor.size >= 4) ? fetchComponent(&arrColor, vi, 3, 1.0f) : 1.0f;
		}
		else
		{
			out[3] = curColor[0];
			out[4] = curColor[1];
			out[5] = curColor[2];
			out[6] = curColor[3];
		}

		if (arrTexCoord[0].enabled && arrTexCoord[0].ptr)
		{
			out[7] = fetchComponent(&arrTexCoord[0], vi, 0, 0.0f);
			out[8] = fetchComponent(&arrTexCoord[0], vi, 1, 0.0f);
		}
		else
		{
			out[7] = curTC[0][0];
			out[8] = curTC[0][1];
		}

		if (arrTexCoord[1].enabled && arrTexCoord[1].ptr)
		{
			out[9]  = fetchComponent(&arrTexCoord[1], vi, 0, 0.0f);
			out[10] = fetchComponent(&arrTexCoord[1], vi, 1, 0.0f);
		}
		else
		{
			out[9]  = 0.0f;
			out[10] = 0.0f;
		}
	}
}

static void uploadScratch(void)
{
	gl3_BindBuffer(GL_ARRAY_BUFFER, vboVerts);
	gl3_BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(scratchUsed * sizeof(GLfloat)), scratchVerts, GL_STREAM_DRAW);
}

// convert Q3-era primitive modes that WebGL lacks (quads/polygons) to triangles
static void convertPrimitive(GLenum mode, GLint count, GLenum *outMode)
{
	*outMode = mode;

	if (mode == GL_QUADS || mode == GL_QUAD_STRIP || mode == GL_POLYGON)
	{
		// handled by index path
	}
}

static void prepareDraw(void)
{
	mat4_t mvp;
	mat4_t *mv = &modelviewStack[modelviewDepth];
	mat4_t *pj = &projectionStack[projectionDepth];

	mat4_mul(&mvp, pj, mv);

	uploadScratch();

	if (extProgramActive && extGLProgram)
	{
		gl3_UseProgram(extGLProgram);
		if (extUMVP != -1)
		{
			gl3_UniformMatrix4fv(extUMVP, 1, GL_FALSE, mvp.m);
		}
		if (extUMV != -1)
		{
			gl3_UniformMatrix4fv(extUMV, 1, GL_FALSE, mv->m);
		}
	}
	else
	{
		gl3_UseProgram(ffProgram);

		if (ffUniformsDirty)
		{
			gl3_UniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);
			gl3_UniformMatrix4fv(uMV, 1, GL_FALSE, mv->m);

			gl3_Uniform1i(uHasTex0, (texUnits[0].enabled && texUnits[0].bound && st.texture2D[0]) ? 1 : 0);
			gl3_Uniform1i(uHasTex1, (texUnits[1].enabled && texUnits[1].bound && st.texture2D[1]) ? 1 : 0);
			gl3_Uniform1i(uMode0, texUnits[0].envMode);
			gl3_Uniform1i(uMode1, texUnits[1].envMode);

			gl3_Uniform1i(uAlphaFunc, alphaTestMode());
			gl3_Uniform1f(uAlphaRef, st.alphaRef);

			gl3_Uniform1i(uClipEnabled, clip0Enabled ? 1 : 0);
			gl3_Uniform4f(uClipPlane, clip0Plane[0], clip0Plane[1], clip0Plane[2], clip0Plane[3]);

			gl3_Uniform1i(uFogEnabled, fogEnabled ? 1 : 0);
			gl3_Uniform1i(uFogMode, fogMode);
			gl3_Uniform4f(uFogColor, fogColor[0], fogColor[1], fogColor[2], fogColor[3]);
			gl3_Uniform1f(uFogDensity, fogDensity);
			gl3_Uniform1f(uFogStart, fogStart);
			gl3_Uniform1f(uFogEnd, fogEnd);

			ffUniformsDirty = 0;
		}
		else
		{
			gl3_UniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);
			gl3_UniformMatrix4fv(uMV, 1, GL_FALSE, mv->m);
		}
	}

}

static void drawWithCurrentProgram(GLenum mode, GLint first, GLsizei count)
{
	prepareDraw();

	// matrices change per draw - always refresh handled above; now the primitives
	if (mode == GL_QUADS)
	{
		GLint quads = count / 4;

		ensureIdx((size_t) quads * 6);
		idxUsed = 0;
		for (GLint q = 0; q < quads; q++)
		{
			GLuint b = (GLuint) first + (GLuint) q * 4;
			scratchIdx[idxUsed++] = b + 0;
			scratchIdx[idxUsed++] = b + 1;
			scratchIdx[idxUsed++] = b + 2;
			scratchIdx[idxUsed++] = b + 0;
			scratchIdx[idxUsed++] = b + 2;
			scratchIdx[idxUsed++] = b + 3;
		}
		gl3_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, vboIdx);
		gl3_BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idxUsed * sizeof(GLuint)), scratchIdx, GL_STREAM_DRAW);
		gl3_DrawElements(GL_TRIANGLES, (GLsizei) idxUsed, GL_UNSIGNED_INT, 0);
	}
	else if (mode == GL_QUAD_STRIP)
	{
		GLint pairs = count / 2 - 1;

		ensureIdx((size_t) pairs * 6);
		idxUsed = 0;
		for (GLint q = 0; q < pairs; q++)
		{
			GLuint b = (GLuint) first + (GLuint) q * 2;
			scratchIdx[idxUsed++] = b + 0;
			scratchIdx[idxUsed++] = b + 1;
			scratchIdx[idxUsed++] = b + 2;
			scratchIdx[idxUsed++] = b + 2;
			scratchIdx[idxUsed++] = b + 1;
			scratchIdx[idxUsed++] = b + 3;
		}
		gl3_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, vboIdx);
		gl3_BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idxUsed * sizeof(GLuint)), scratchIdx, GL_STREAM_DRAW);
		gl3_DrawElements(GL_TRIANGLES, (GLsizei) idxUsed, GL_UNSIGNED_INT, 0);
	}
	else if (mode == GL_POLYGON)
	{
		GLint tris = count - 2;

		ensureIdx((size_t) tris * 3);
		idxUsed = 0;
		for (GLint t = 0; t < tris; t++)
		{
			GLuint b = (GLuint) first;
			scratchIdx[idxUsed++] = b + 0;
			scratchIdx[idxUsed++] = b + (GLuint) t + 1;
			scratchIdx[idxUsed++] = b + (GLuint) t + 2;
		}
		gl3_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, vboIdx);
		gl3_BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idxUsed * sizeof(GLuint)), scratchIdx, GL_STREAM_DRAW);
		gl3_DrawElements(GL_TRIANGLES, (GLsizei) idxUsed, GL_UNSIGNED_INT, 0);
	}
	else
	{
		gl3_DrawArrays(mode, 0, count);
	}
}

// draw with explicit client-side indices (already-repacked vertex scratch)
static void drawWithCurrentProgramIndexed(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	prepareDraw();
	// WebGL2 accepts all three engine index formats. Upload directly: no
	// temporary allocation/copy, and byte/short indices retain their size.
	gl3_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, vboIdx);
	gl3_BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t) count * attribTypeSize(type)), indices, GL_STREAM_DRAW);
	gl3_DrawElements(mode, count, type, 0);
}

// ---------------------------------------------------------------------------
// shaders
// ---------------------------------------------------------------------------
static const char *ffVertSrc =
	"#version 300 es\n"
	"precision highp float;\n"
	"layout(location=0) in vec3 aPos;\n"
	"layout(location=1) in vec4 aColor;\n"
	"layout(location=2) in vec2 aTC0;\n"
	"layout(location=3) in vec2 aTC1;\n"
	"uniform mat4 uMVP;\n"
	"uniform mat4 uMV;\n"
	"uniform vec4 uClipPlane;\n"
	"out vec4 vColor;\n"
	"out vec2 vTC0;\n"
	"out vec2 vTC1;\n"
	"out vec3 vEyePos;\n"
	"void main()\n"
	"{\n"
	"    vec4 eye = uMV * vec4(aPos, 1.0);\n"
	"    vEyePos = eye.xyz;\n"
	"    vColor = aColor;\n"
	"    vTC0 = aTC0;\n"
	"    vTC1 = aTC1;\n"
	"    gl_Position = uMVP * vec4(aPos, 1.0);\n"
	"}\n";

static const char *ffFragSrc =
	"#version 300 es\n"
	"precision highp float;\n"
	"in vec4 vColor;\n"
	"in vec2 vTC0;\n"
	"in vec2 vTC1;\n"
	"in vec3 vEyePos;\n"
	"uniform sampler2D uTex0;\n"
	"uniform sampler2D uTex1;\n"
	"uniform int uHasTex0;\n"
	"uniform int uHasTex1;\n"
	"uniform int uMode0;\n"
	"uniform int uMode1;\n"
	"uniform int uAlphaFunc;\n"
	"uniform float uAlphaRef;\n"
	"uniform vec4 uClipPlane;\n"
	"uniform int uClipEnabled;\n"
	"uniform int uFogEnabled;\n"
	"uniform int uFogMode;\n"
	"uniform vec4 uFogColor;\n"
	"uniform float uFogDensity;\n"
	"uniform float uFogStart;\n"
	"uniform float uFogEnd;\n"
	"out vec4 fragColor;\n"
	"vec4 applyUnit(vec4 base, vec4 tex, int mode)\n"
	"{\n"
	"    if (mode == 0) return tex;\n"
	"    if (mode == 1) return base * tex;\n"
	"    if (mode == 2) return vec4(min(base.rgb + tex.rgb, vec3(1.0)), base.a * tex.a);\n"
	"    if (mode == 3) return vec4(mix(base.rgb, tex.rgb, tex.a), base.a);\n"
	"    return base;\n"
	"}\n"
	"void main()\n"
	"{\n"
	"    if (uClipEnabled == 1 && dot(vec4(vEyePos, 1.0), uClipPlane) < 0.0) discard;\n"
	"    vec4 c = vColor;\n"
	"    if (uHasTex0 == 1) c = applyUnit(c, texture(uTex0, vTC0), uMode0);\n"
	"    if (uHasTex1 == 1) c = applyUnit(c, texture(uTex1, vTC1), uMode1);\n"
	"    if (uAlphaFunc >= 1 && uAlphaFunc <= 8)\n"
	"    {\n"
	"        bool pass = true;\n"
	"        if      (uAlphaFunc == 1) pass = false;\n"
	"        else if (uAlphaFunc == 2) pass = c.a < uAlphaRef;\n"
	"        else if (uAlphaFunc == 3) pass = c.a == uAlphaRef;\n"
	"        else if (uAlphaFunc == 4) pass = c.a <= uAlphaRef;\n"
	"        else if (uAlphaFunc == 5) pass = c.a > uAlphaRef;\n"
	"        else if (uAlphaFunc == 6) pass = c.a != uAlphaRef;\n"
	"        else if (uAlphaFunc == 7) pass = c.a >= uAlphaRef;\n"
	"        if (!pass) discard;\n"
	"    }\n"
	"    if (uFogEnabled == 1)\n"
	"    {\n"
	"        float d = abs(vEyePos.z);\n"
	"        float f = 1.0;\n"
	"        if (uFogMode == 0)      f = clamp((uFogEnd - d) / (uFogEnd - uFogStart), 0.0, 1.0);\n"
	"        else if (uFogMode == 1) f = clamp(exp(-uFogDensity * d), 0.0, 1.0);\n"
	"        else                    f = clamp(exp(-uFogDensity * uFogDensity * d * d), 0.0, 1.0);\n"
	"        c.rgb = mix(uFogColor.rgb, c.rgb, f);\n"
	"    }\n"
	"    fragColor = c;\n"
	"}\n";

static GLuint compileShader(GLenum type, const char *src)
{
	GLuint s = gl3_CreateShader(type);

	gl3_ShaderSource(s, 1, &src, NULL);
	gl3_CompileShader(s);

	GLint ok = 0;
	gl3_GetShaderiv(s, GL_COMPILE_STATUS, &ok);

	if (!ok)
	{
		char log[1024];
		GLsizei len = 0;
		gl3_GetShaderInfoLog(s, sizeof(log), &len, log);
		fprintf(stderr, "ETWebGL: shader compile failed: %s\n", log);
		gl3_DeleteShader(s);
		return 0;
	}

	return s;
}

static GLuint buildFFProgram(void)
{
	GLuint prog = gl3_CreateProgram();
	GLuint v = compileShader(GL_VERTEX_SHADER, ffVertSrc);
	GLuint f = compileShader(GL_FRAGMENT_SHADER, ffFragSrc);

	if (!v || !f)
	{
		if (v) gl3_DeleteShader(v);
		if (f) gl3_DeleteShader(f);
		gl3_DeleteProgram(prog);
		return 0;
	}

	gl3_AttachShader(prog, v);
	gl3_AttachShader(prog, f);
	gl3_LinkProgram(prog);
	gl3_DeleteShader(v);
	gl3_DeleteShader(f);

	GLint ok = 0;
	gl3_GetProgramiv(prog, GL_LINK_STATUS, &ok);

	if (!ok)
	{
		char log[1024];
		GLsizei len = 0;
		gl3_GetProgramInfoLog(prog, sizeof(log), &len, log);
		fprintf(stderr, "ETWebGL: program link failed: %s\n", log);
		gl3_DeleteProgram(prog);
		return 0;
	}

	uMVP = gl3_GetUniformLocation(prog, "uMVP");
	uMV = gl3_GetUniformLocation(prog, "uMV");
	uTex0 = gl3_GetUniformLocation(prog, "uTex0");
	uTex1 = gl3_GetUniformLocation(prog, "uTex1");
	uHasTex0 = gl3_GetUniformLocation(prog, "uHasTex0");
	uHasTex1 = gl3_GetUniformLocation(prog, "uHasTex1");
	uMode0 = gl3_GetUniformLocation(prog, "uMode0");
	uMode1 = gl3_GetUniformLocation(prog, "uMode1");
	uAlphaFunc = gl3_GetUniformLocation(prog, "uAlphaFunc");
	uAlphaRef = gl3_GetUniformLocation(prog, "uAlphaRef");
	uClipEnabled = gl3_GetUniformLocation(prog, "uClipEnabled");
	uClipPlane = gl3_GetUniformLocation(prog, "uClipPlane");
	uFogEnabled = gl3_GetUniformLocation(prog, "uFogEnabled");
	uFogMode = gl3_GetUniformLocation(prog, "uFogMode");
	uFogColor = gl3_GetUniformLocation(prog, "uFogColor");
	uFogDensity = gl3_GetUniformLocation(prog, "uFogDensity");
	uFogStart = gl3_GetUniformLocation(prog, "uFogStart");
	uFogEnd = gl3_GetUniformLocation(prog, "uFogEnd");

	gl3_UseProgram(prog);
	gl3_Uniform1i(uTex0, 0);
	gl3_Uniform1i(uTex1, 1);

	return prog;
}

// ---------------------------------------------------------------------------
// ARB shader object emulation (GLSL 110 -> GLSL ES 300 transpile)
// ---------------------------------------------------------------------------
typedef struct
{
	GLuint glProg;
	GLuint glVert;
	GLuint glFrag;
	GLint  uMVP, uMV;
	int    used;
} arbProgram_t;

#define ARB_MAX_PROGRAMS 16
static arbProgram_t arbPrograms[ARB_MAX_PROGRAMS];


typedef struct
{
	GLenum  type;
	GLuint  glShader;
	char   *source;
} arbShader_t;

#define ARB_MAX_SHADERS 32
static arbShader_t arbShaders[ARB_MAX_SHADERS];


static char *dupStr(const char *s)
{
	size_t len = strlen(s);
	char *d = (char *) malloc(len + 1);
	memcpy(d, s, len + 1);
	return d;
}

static void strReplaceAll(char *buf, size_t bufSize, const char *from, const char *to)
{
	char  tmp[16384];
	size_t fromLen = strlen(from);
	size_t pos = 0;
	size_t out = 0;

	while (buf[pos] && out + strlen(to) < bufSize)
	{
		if (!strncmp(buf + pos, from, fromLen))
		{
			strcpy(tmp + out, to);
			out += strlen(to);
			pos += fromLen;
		}
		else
		{
			tmp[out++] = buf[pos++];
		}
	}
	tmp[out] = '\0';
	strcpy(buf, tmp);
}

/*
 * Transpile the engine's GLSL 110 shaders to GLSL ES 300. Written for the
 * gamma postprocess but general enough for simple 110 shaders.
 */
static void transpile110(char *src, size_t srcCap, int isVertex)
{
	strReplaceAll(src, srcCap, "#version 110", "");
	strReplaceAll(src, srcCap, "texture2D", "texture");
	strReplaceAll(src, srcCap, "gl_TexCoord[0]", "vTC0");
	strReplaceAll(src, srcCap, "gl_TexCoord[1]", "vTC1");

	if (isVertex)
	{
		strReplaceAll(src, srcCap, "varying", "out");
		strReplaceAll(src, srcCap, "attribute", "in");
	}
	else
	{
		strReplaceAll(src, srcCap, "varying", "in");
		strReplaceAll(src, srcCap, "gl_FragColor", "etFragColor");
	}
}

static arbShader_t *arbShader(GLhandleARB handle)
{
	return handle >= 1 && handle <= ARB_MAX_SHADERS && arbShaders[handle - 1].glShader
		? &arbShaders[handle - 1] : NULL;
}

static arbProgram_t *arbProgram(GLhandleARB handle)
{
	int index = (int) handle - ARB_MAX_SHADERS - 1;
	return index >= 0 && index < ARB_MAX_PROGRAMS && arbPrograms[index].glProg
		? &arbPrograms[index] : NULL;
}

GLhandleARB glCreateShaderObjectARB(GLenum shaderType)
{
	for (int i = 0; i < ARB_MAX_SHADERS; i++)
	{
		if (!arbShaders[i].glShader)
		{
			arbShaders[i].type = shaderType;
			arbShaders[i].glShader = gl3_CreateShader(shaderType);
			return arbShaders[i].glShader ? (GLhandleARB) (i + 1) : 0;
		}
	}
	lastError = GL_OUT_OF_MEMORY;
	return 0;
}

void glShaderSourceARB(GLhandleARB shader, GLsizei count, const GLcharARB **string, const GLint *length)
{
	arbShader_t *s = arbShader(shader);
	if (!s) { lastError = GL_INVALID_VALUE; return; }
	size_t total = 1;

	for (GLsizei i = 0; i < count; i++)
	{
		total += length && length[i] >= 0 ? (size_t) length[i] : strlen(string[i]);
	}

	if (s->source)
	{
		free(s->source);
	}
	s->source = (char *) malloc(total * 2 + 512);
	s->source[0] = '\0';

	for (GLsizei i = 0; i < count; i++)
	{
		if (length && length[i] >= 0)
		{
			strncat(s->source, string[i], (size_t) length[i]);
		}
		else
		{
			strcat(s->source, string[i]);
		}
	}
}

void glCompileShaderARB(GLhandleARB shader)
{
	arbShader_t *s = arbShader(shader);
	if (!s) { lastError = GL_INVALID_VALUE; return; }
	char buf[16384];

	if (!s->source)
	{
		return;
	}

	strncpy(buf, s->source, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	transpile110(buf, sizeof(buf), s->type == GL_VERTEX_SHADER_ARB);

	const char *preamble =
		(s->type == GL_VERTEX_SHADER_ARB) ?
		"#version 300 es\n"
		"precision highp float;\n"
		"in vec3 aPos;\n"
		"in vec4 aColor;\n"
		"in vec2 aTC0;\n"
		"in vec2 aTC1;\n"
		"uniform mat4 uMVP;\n"
		"uniform mat4 uMV;\n"
		"#define gl_Vertex vec4(aPos, 1.0)\n"
		"#define gl_Color aColor\n"
		"#define gl_MultiTexCoord0 aTC0\n"
		"#define gl_MultiTexCoord1 aTC1\n"
		"#define gl_ModelViewProjectionMatrix uMVP\n"
		"#define gl_ModelViewMatrix uMV\n"
		:
		"#version 300 es\n"
		"precision highp float;\n"
		"out vec4 etFragColor;\n";

	// allocate full source with preamble
	char *full = (char *) malloc(strlen(preamble) + strlen(buf) + 64);

	// insert out declarations for the varyings the vertex stage writes
	if (s->type == GL_VERTEX_SHADER_ARB)
	{
		full[0] = '\0';
		strcat(full, preamble);
		strcat(full, "out vec2 vTC0;\nout vec2 vTC1;\n");
		strcat(full, buf);
	}
	else
	{
		full[0] = '\0';
		strcat(full, preamble);
		strcat(full, "in vec2 vTC0;\nin vec2 vTC1;\n");
		strcat(full, buf);
	}

	gl3_ShaderSource(s->glShader, 1, (const GLchar *const *) &full, NULL);
	gl3_CompileShader(s->glShader);

	GLint ok = 0;
	gl3_GetShaderiv(s->glShader, GL_COMPILE_STATUS, &ok);

	if (!ok)
	{
		char log[1024];
		GLsizei len = 0;
		gl3_GetShaderInfoLog(s->glShader, sizeof(log), &len, log);
		fprintf(stderr, "ETWebGL: ARB shader compile failed:\n%s\n---- src ----\n%s\n", log, full);
	}

	free(full);
}

GLhandleARB glCreateProgramObjectARB(void)
{
	for (int i = 0; i < ARB_MAX_PROGRAMS; i++)
	{
		if (!arbPrograms[i].glProg)
		{
			arbPrograms[i].glProg = gl3_CreateProgram();
			arbPrograms[i].uMVP = arbPrograms[i].uMV = -1;
			return arbPrograms[i].glProg ? (GLhandleARB) (ARB_MAX_SHADERS + i + 1) : 0;
		}
	}
	lastError = GL_OUT_OF_MEMORY;
	return 0;
}

void glAttachObjectARB(GLhandleARB container, GLhandleARB obj)
{
	arbProgram_t *p = arbProgram(container);
	arbShader_t *s = arbShader(obj);
	if (!p || !s) { lastError = GL_INVALID_VALUE; return; }

	gl3_AttachShader(p->glProg, s->glShader);
}

void glDetachObjectARB(GLhandleARB container, GLhandleARB obj)
{
	arbProgram_t *p = arbProgram(container);
	arbShader_t *s = arbShader(obj);
	if (!p || !s) { lastError = GL_INVALID_VALUE; return; }

	gl3_DetachShader(p->glProg, s->glShader);
}

void glLinkProgramARB(GLhandleARB programObj)
{
	arbProgram_t *p = arbProgram(programObj);
	if (!p) { lastError = GL_INVALID_VALUE; return; }

	gl3_BindAttribLocation(p->glProg, 0, "aPos");
	gl3_BindAttribLocation(p->glProg, 1, "aColor");
	gl3_BindAttribLocation(p->glProg, 2, "aTC0");
	gl3_BindAttribLocation(p->glProg, 3, "aTC1");
	gl3_LinkProgram(p->glProg);
	p->uMVP = gl3_GetUniformLocation(p->glProg, "uMVP");
	p->uMV = gl3_GetUniformLocation(p->glProg, "uMV");
}

void glUseProgramObjectARB(GLhandleARB programObj)
{
	if (programObj == 0)
	{
		extProgramActive = 0;
		extGLProgram = 0;
		gl3_UseProgram(ffProgram);
		markUniformsDirty();
		return;
	}

	arbProgram_t *p = arbProgram(programObj);
	if (!p) { lastError = GL_INVALID_VALUE; return; }

	extProgramActive = 1;
	extGLProgram = p->glProg;
	gl3_UseProgram(extGLProgram);
	extUMVP = p->uMVP;
	extUMV = p->uMV;
}

void glDeleteObjectARB(GLhandleARB obj)
{
	arbShader_t *s = arbShader(obj);
	arbProgram_t *p = arbProgram(obj);
	if (s)
	{
		gl3_DeleteShader(s->glShader);
		free(s->source);
		memset(s, 0, sizeof(*s));
	}
	else if (p)
	{
		if (extGLProgram == p->glProg) glUseProgramObjectARB(0);
		gl3_DeleteProgram(p->glProg);
		memset(p, 0, sizeof(*p));
	}
}

void glGetObjectParameterivARB(GLhandleARB obj, GLenum pname, GLint *params)
{
	arbShader_t *s = arbShader(obj);
	arbProgram_t *p = arbProgram(obj);
	if (s && pname == GL_OBJECT_COMPILE_STATUS_ARB)
		gl3_GetShaderiv(s->glShader, GL_COMPILE_STATUS, params);
	else if (p && pname == GL_OBJECT_LINK_STATUS_ARB)
		gl3_GetProgramiv(p->glProg, GL_LINK_STATUS, params);
	else *params = (s || p) ? 1 : 0;
}

void glGetInfoLogARB(GLhandleARB obj, GLsizei maxLength, GLsizei *length, GLcharARB *infoLog)
{
	arbShader_t *s = arbShader(obj);
	arbProgram_t *p = arbProgram(obj);
	if (s) gl3_GetShaderInfoLog(s->glShader, maxLength, length, infoLog);
	else if (p) gl3_GetProgramInfoLog(p->glProg, maxLength, length, infoLog);
	else
	{
		if (infoLog && maxLength > 0) infoLog[0] = '\0';
		if (length) *length = 0;
	}
}

GLint glGetUniformLocation(GLhandleARB programObj, const GLchar *name)
{
	arbProgram_t *p = arbProgram(programObj);
	return p ? gl3_GetUniformLocation(p->glProg, name) : -1;
}

GLint glGetUniformLocationARB(GLhandleARB programObj, const GLcharARB *name)
{
	return glGetUniformLocation(programObj, name);
}

void glGetShaderiv(GLhandleARB shader, GLenum pname, GLint *params)
{
	arbShader_t *s = arbShader(shader);
	if (s) gl3_GetShaderiv(s->glShader, pname, params);
	else *params = 0;
}

void glGetQueryivARB(GLenum target, GLenum pname, GLint *params)
{
	(void) target;
	if (pname == GL_QUERY_COUNTER_BITS_ARB)
	{
		*params = 0;    // no occlusion query support
	}
}

// ---------------------------------------------------------------------------
// immediate mode capture
// ---------------------------------------------------------------------------
static int   imActive = 0;
static GLenum imMode = 0;
static GLsizei imCount = 0;

static void imVert(GLfloat x, GLfloat y, GLfloat z)
{
	if (imCount >= SHIM_MAX_IMMEDIATE)
	{
		return;
	}

	ensureScratch((size_t)(imCount + 1) * 11);

	GLfloat *out = scratchVerts + (size_t) imCount * 11;

	out[0] = x;
	out[1] = y;
	out[2] = z;
	out[3] = curColor[0];
	out[4] = curColor[1];
	out[5] = curColor[2];
	out[6] = curColor[3];
	out[7] = curTC[0][0];
	out[8] = curTC[0][1];
	out[9] = curTC[1][0];
	out[10] = curTC[1][1];
	imCount++;
}

static void imFlush(void)
{
	if (!imCount)
	{
		return;
	}

	statDraw((size_t) imCount);

	scratchUsed = (size_t) imCount * 11;

	// repack reads from client arrays; for immediate data the scratch is
	// already filled, so draw directly without repacking
	mat4_t mvp;
	mat4_t *mv = &modelviewStack[modelviewDepth];
	mat4_t *pj = &projectionStack[projectionDepth];

	mat4_mul(&mvp, pj, mv);

	uploadScratch();

	if (extProgramActive && extGLProgram)
	{
		gl3_UseProgram(extGLProgram);
		if (extUMVP != -1)
		{
			gl3_UniformMatrix4fv(extUMVP, 1, GL_FALSE, mvp.m);
		}
	}
	else
	{
		gl3_UseProgram(ffProgram);
		gl3_UniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);
		gl3_UniformMatrix4fv(uMV, 1, GL_FALSE, mv->m);
		gl3_Uniform1i(uHasTex0, (texUnits[0].enabled && texUnits[0].bound && st.texture2D[0]) ? 1 : 0);
		gl3_Uniform1i(uHasTex1, (texUnits[1].enabled && texUnits[1].bound && st.texture2D[1]) ? 1 : 0);
		gl3_Uniform1i(uMode0, texUnits[0].envMode);
		gl3_Uniform1i(uMode1, texUnits[1].envMode);
		gl3_Uniform1i(uAlphaFunc, alphaTestMode());
		gl3_Uniform1f(uAlphaRef, st.alphaRef);
		gl3_Uniform1i(uClipEnabled, clip0Enabled ? 1 : 0);
		gl3_Uniform4f(uClipPlane, clip0Plane[0], clip0Plane[1], clip0Plane[2], clip0Plane[3]);
		gl3_Uniform1i(uFogEnabled, fogEnabled ? 1 : 0);
		gl3_Uniform1i(uFogMode, fogMode);
		gl3_Uniform4f(uFogColor, fogColor[0], fogColor[1], fogColor[2], fogColor[3]);
		gl3_Uniform1f(uFogDensity, fogDensity);
		gl3_Uniform1f(uFogStart, fogStart);
		gl3_Uniform1f(uFogEnd, fogEnd);
	}

	if (imMode == GL_QUADS)
	{
		GLint quads = imCount / 4;

		ensureIdx((size_t) quads * 6);
		idxUsed = 0;
		for (GLint q = 0; q < quads; q++)
		{
			GLuint b = (GLuint) q * 4;
			scratchIdx[idxUsed++] = b + 0;
			scratchIdx[idxUsed++] = b + 1;
			scratchIdx[idxUsed++] = b + 2;
			scratchIdx[idxUsed++] = b + 0;
			scratchIdx[idxUsed++] = b + 2;
			scratchIdx[idxUsed++] = b + 3;
		}
		gl3_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, vboIdx);
		gl3_BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idxUsed * sizeof(GLuint)), scratchIdx, GL_STREAM_DRAW);
		gl3_DrawElements(GL_TRIANGLES, (GLsizei) idxUsed, GL_UNSIGNED_INT, 0);
	}
	else if (imMode == GL_QUAD_STRIP)
	{
		GLint pairs = imCount / 2 - 1;

		ensureIdx((size_t) pairs * 6);
		idxUsed = 0;
		for (GLint q = 0; q < pairs; q++)
		{
			GLuint b = (GLuint) q * 2;
			scratchIdx[idxUsed++] = b + 0;
			scratchIdx[idxUsed++] = b + 1;
			scratchIdx[idxUsed++] = b + 2;
			scratchIdx[idxUsed++] = b + 2;
			scratchIdx[idxUsed++] = b + 1;
			scratchIdx[idxUsed++] = b + 3;
		}
		gl3_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, vboIdx);
		gl3_BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idxUsed * sizeof(GLuint)), scratchIdx, GL_STREAM_DRAW);
		gl3_DrawElements(GL_TRIANGLES, (GLsizei) idxUsed, GL_UNSIGNED_INT, 0);
	}
	else if (imMode == GL_POLYGON)
	{
		GLint tris = imCount - 2;

		ensureIdx((size_t) tris * 3);
		idxUsed = 0;
		for (GLint t = 0; t < tris; t++)
		{
			GLuint b = 0;
			scratchIdx[idxUsed++] = b + 0;
			scratchIdx[idxUsed++] = b + (GLuint) t + 1;
			scratchIdx[idxUsed++] = b + (GLuint) t + 2;
		}
		gl3_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, vboIdx);
		gl3_BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idxUsed * sizeof(GLuint)), scratchIdx, GL_STREAM_DRAW);
		gl3_DrawElements(GL_TRIANGLES, (GLsizei) idxUsed, GL_UNSIGNED_INT, 0);
	}
	else
	{
		gl3_DrawArrays(imMode, 0, imCount);
	}

	imCount = 0;
}

// ---------------------------------------------------------------------------
// GL 1.1 API implementation
// ---------------------------------------------------------------------------

void glClear(GLbitfield mask)
{
	if (imActive)
	{
		return;
	}
#ifdef ETWEBGL_DEBUG_STATS
	if ((mask & GL_COLOR_BUFFER_BIT) && (statFrames = statFrames + 1) >= 60)
	{
		statFrames = 0;
		fprintf(stderr, "ETWebGL stats: %lu draws %lu verts\n", statDraws, statVerts);
		statDraws = statVerts = 0;
	}
#endif
	gl3_Clear(mask);
}

void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	st.clearColor[0] = red;
	st.clearColor[1] = green;
	st.clearColor[2] = blue;
	st.clearColor[3] = alpha;
	gl3_ClearColor(red, green, blue, alpha);
}

void glClearDepth(GLclampd depth)
{
	st.clearDepth = depth;
	gl3_ClearDepthf((GLfloat) depth);
}

void glClearStencil(GLint s)
{
	st.clearStencil = s;
	gl3_ClearStencil(s);
}

GLenum glGetError(void)
{
	GLenum e = lastError;

	lastError = GL_NO_ERROR;
	// Keep pending backend errors available after reporting a shim error.
	return e != GL_NO_ERROR ? e : gl3_GetError();
}

void glFinish(void)
{
	gl3_Finish();
}

void glFlush(void)
{
	gl3_Flush();
}

void glHint(GLenum target, GLenum mode)
{
	(void) target;
	(void) mode;
}

void glEnable(GLenum cap)
{
	markUniformsDirty();

	switch (cap)
	{
	case GL_DEPTH_TEST:     st.depthTest = 1; break;
	case GL_BLEND:          st.blend = 1; break;
	case GL_ALPHA_TEST:     st.alphaTest = 1; break;
	case GL_CULL_FACE:      st.cullFace = 1; break;
	case GL_SCISSOR_TEST:   st.scissorTest = 1; break;
	case GL_STENCIL_TEST:   st.stencilTest = 1; break;
	case GL_FOG:            fogEnabled = 1; break;
	case GL_CLIP_PLANE0:    clip0Enabled = 1; break;
	case GL_TEXTURE_2D:
		if (curTMU < SHIM_MAX_TMUS)
		{
			st.texture2D[curTMU] = 1;
			texUnits[curTMU].enabled = GL_TRUE;
		}
		break;
	default: break;
	}

	if (cap != GL_ALPHA_TEST && cap != GL_FOG && cap != GL_TEXTURE_2D && cap != GL_CLIP_PLANE0)
		gl3_Enable(cap);
}

void glDisable(GLenum cap)
{
	markUniformsDirty();

	switch (cap)
	{
	case GL_DEPTH_TEST:     st.depthTest = 0; break;
	case GL_BLEND:          st.blend = 0; break;
	case GL_ALPHA_TEST:     st.alphaTest = 0; break;
	case GL_CULL_FACE:      st.cullFace = 0; break;
	case GL_SCISSOR_TEST:   st.scissorTest = 0; break;
	case GL_STENCIL_TEST:   st.stencilTest = 0; break;
	case GL_FOG:            fogEnabled = 0; break;
	case GL_CLIP_PLANE0:    clip0Enabled = 0; break;
	case GL_TEXTURE_2D:
		if (curTMU < SHIM_MAX_TMUS)
		{
			st.texture2D[curTMU] = 0;
			texUnits[curTMU].enabled = GL_FALSE;
		}
		break;
	default: break;
	}

	if (cap != GL_ALPHA_TEST && cap != GL_FOG && cap != GL_TEXTURE_2D && cap != GL_CLIP_PLANE0)
		gl3_Disable(cap);
}

GLboolean glIsEnabled(GLenum cap)
{
	// These fixed-function capabilities live in the shim, outside WebGL's enums.
	switch (cap)
	{
	case GL_ALPHA_TEST: return st.alphaTest ? GL_TRUE : GL_FALSE;
	case GL_FOG: return fogEnabled ? GL_TRUE : GL_FALSE;
	case GL_CLIP_PLANE0: return clip0Enabled ? GL_TRUE : GL_FALSE;
	case GL_TEXTURE_2D: return texUnits[curTMU].enabled;
	case GL_VERTEX_ARRAY: return arrVertex.enabled;
	case GL_COLOR_ARRAY: return arrColor.enabled;
	case GL_TEXTURE_COORD_ARRAY: return arrTexCoord[clientTMU].enabled;
	default: return gl3_IsEnabled(cap);
	}
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	vpX = x; vpY = y; vpW = width; vpH = height;
	gl3_Viewport(x, y, width, height);
}

void glMatrixMode(GLenum mode)
{
	curMatrixMode = mode;
}

void glLoadIdentity(void)
{
	int depth, maxDepth;
	mat4_t *stack = currentMatrixStack(&depth, &maxDepth);

	mat4_identity(&stack[depth]);
	markUniformsDirty();
}

void glLoadMatrixf(const GLfloat *m)
{
	int depth, maxDepth;
	mat4_t *stack = currentMatrixStack(&depth, &maxDepth);

	memcpy(stack[depth].m, m, 16 * sizeof(GLfloat));
	markUniformsDirty();
}

void glLoadMatrixd(const GLdouble *m)
{
	int depth, maxDepth;
	mat4_t *stack = currentMatrixStack(&depth, &maxDepth);

	for (int i = 0; i < 16; i++)
	{
		stack[depth].m[i] = (GLfloat) m[i];
	}
	markUniformsDirty();
}

void glMultMatrixf(const GLfloat *m)
{
	int depth, maxDepth;
	mat4_t *stack = currentMatrixStack(&depth, &maxDepth);
	mat4_t r, mm;

	memcpy(mm.m, m, 16 * sizeof(GLfloat));
	mat4_mul(&r, &stack[depth], &mm);
	stack[depth] = r;
	markUniformsDirty();
}

void glPushMatrix(void)
{
	int depth, maxDepth;
	mat4_t *stack = currentMatrixStack(&depth, &maxDepth);

	if (depth + 1 < maxDepth)
	{
		stack[depth + 1] = stack[depth];
		if (curMatrixMode == GL_MODELVIEW)
		{
			modelviewDepth++;
		}
		else if (curMatrixMode == GL_PROJECTION)
		{
			projectionDepth++;
		}
		else
		{
			textureDepth[curTMU]++;
		}
	}
}

void glPopMatrix(void)
{
	if (curMatrixMode == GL_MODELVIEW && modelviewDepth > 0)
	{
		modelviewDepth--;
	}
	else if (curMatrixMode == GL_PROJECTION && projectionDepth > 0)
	{
		projectionDepth--;
	}
	else if (curMatrixMode == GL_TEXTURE && textureDepth[curTMU] > 0)
	{
		textureDepth[curTMU]--;
	}
	markUniformsDirty();
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat m[16] =
	{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		x, y, z, 1
	};

	glMultMatrixf(m);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat rad = angle * (GLfloat) M_PI / 180.0f;
	GLfloat c = cosf(rad), s = sinf(rad);
	GLfloat len = sqrtf(x * x + y * y + z * z);

	if (len == 0.0f)
	{
		return;
	}
	x /= len; y /= len; z /= len;

	GLfloat m[16] =
	{
		x * x * (1 - c) + c,     y * x * (1 - c) + z * s, x * z * (1 - c) - y * s, 0,
		x * y * (1 - c) - z * s, y * y * (1 - c) + c,     y * z * (1 - c) + x * s, 0,
		x * z * (1 - c) + y * s, y * z * (1 - c) - x * s, z * z * (1 - c) + c,     0,
		0, 0, 0, 1
	};

	glMultMatrixf(m);
}

void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat m[16] =
	{
		x, 0, 0, 0,
		0, y, 0, 0,
		0, 0, z, 0,
		0, 0, 0, 1
	};

	glMultMatrixf(m);
}

void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	GLfloat m[16];

	memset(m, 0, sizeof(m));
	m[0] = (GLfloat) (2.0 / (right - left));
	m[5] = (GLfloat) (2.0 / (top - bottom));
	m[10] = (GLfloat) (-2.0 / (zFar - zNear));
	m[12] = (GLfloat) (-(right + left) / (right - left));
	m[13] = (GLfloat) (-(top + bottom) / (top - bottom));
	m[14] = (GLfloat) (-(zFar + zNear) / (zFar - zNear));
	m[15] = 1.0f;
	glMultMatrixf(m);
}

void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	GLfloat m[16];

	memset(m, 0, sizeof(m));
	m[0] = (GLfloat) (2.0 * zNear / (right - left));
	m[5] = (GLfloat) (2.0 * zNear / (top - bottom));
	m[8] = (GLfloat) ((right + left) / (right - left));
	m[9] = (GLfloat) ((top + bottom) / (top - bottom));
	m[10] = (GLfloat) (-(zFar + zNear) / (zFar - zNear));
	m[11] = -1.0f;
	m[14] = (GLfloat) (-2.0 * zFar * zNear / (zFar - zNear));
	glMultMatrixf(m);
}

void glDepthRange(GLclampd near_val, GLclampd far_val)
{
	depthNear = (GLfloat) near_val;
	depthFar = (GLfloat) far_val;
	gl3_DepthRangef(depthNear, depthFar);
}

void glDepthFunc(GLenum func)
{
	st.depthFunc = func;
	gl3_DepthFunc(func);
}

void glDepthMask(GLboolean flag)
{
	st.depthMask = flag;
	gl3_DepthMask(flag);
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
	st.colorMaskR = red; st.colorMaskG = green; st.colorMaskB = blue; st.colorMaskA = alpha;
	gl3_ColorMask(red, green, blue, alpha);
}

void glBlendFunc(GLenum sfactor, GLenum dfactor)
{
	st.sfSrc = sfactor;
	st.sfDst = dfactor;
	gl3_BlendFunc(sfactor, dfactor);
}

void glAlphaFunc(GLenum func, GLclampf ref)
{
	if (func < GL_NEVER || func > GL_ALWAYS) { lastError = GL_INVALID_ENUM; return; }
	st.alphaFunc = func;
	st.alphaRef = fminf(1.0f, fmaxf(0.0f, ref));
	markUniformsDirty();
}

void glLogicOp(GLenum opcode)
{
	(void) opcode;
}

void glCullFace(GLenum mode)
{
	st.cullMode = mode;
	gl3_CullFace(mode);
}

void glFrontFace(GLenum mode)
{
	st.frontFace = mode;
	gl3_FrontFace(mode);
}

void glShadeModel(GLenum mode)
{
	st.shadeModel = mode;
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	st.scissorX = x; st.scissorY = y; st.scissorW = width; st.scissorH = height;
	gl3_Scissor(x, y, width, height);
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
	st.stencilFunc = func;
	st.stencilRef = ref;
	st.stencilValueMask = mask;
	gl3_StencilFunc(func, ref, mask);
}

void glStencilMask(GLuint mask)
{
	st.stencilMask = mask;
	gl3_StencilMask(mask);
}

void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
	st.stencilFail = fail;
	st.stencilZFail = zfail;
	st.stencilZPass = zpass;
	gl3_StencilOp(fail, zfail, zpass);
}

void glPolygonMode(GLenum face, GLenum mode)
{
	(void) face;
	(void) mode;    // no wireframe in WebGL
}

void glPolygonOffset(GLfloat factor, GLfloat units)
{
	st.polygonOffsetFactor = factor;
	st.polygonOffsetUnits = units;
	gl3_PolygonOffset(factor, units);
}

void glLineWidth(GLfloat width)
{
	gl3_LineWidth(width < 1.0f ? 1.0f : width);
}

void glPointSize(GLfloat size)
{
	(void) size;    // WebGL has no point size
}

void glClipPlane(GLenum plane, const GLdouble *equation)
{
	if (plane != GL_CLIP_PLANE0)
	{
		return;
	}

	// Store the equation in eye coordinates at call time, using inv(MV)^T.
	mat4_t *mv = &modelviewStack[modelviewDepth];
	GLfloat a = mv->m[0], b = mv->m[4], c = mv->m[8], d = mv->m[12];
	GLfloat e = mv->m[1], f = mv->m[5], g = mv->m[9], h = mv->m[13];
	GLfloat i = mv->m[2], j = mv->m[6], k = mv->m[10], l = mv->m[14];
	// drop translation column for the normal part; compute a 3x3 inverse
	GLfloat A = f * k - g * j;
	GLfloat B = g * i - e * k;
	GLfloat C = e * j - f * i;
	GLfloat det = a * A + b * B + c * C;

	if (det == 0.0f)
	{
		for (int component = 0; component < 4; component++) clip0Plane[component] = (GLfloat) equation[component];
	}
	else
	{
		GLfloat idet = 1.0f / det;

		// inv3x3 rows
		GLfloat r0[3] = { A * idet, (c * j - b * k) * idet, (b * g - c * f) * idet };
		GLfloat r1[3] = { B * idet, (a * k - c * i) * idet, (c * e - a * g) * idet };
		GLfloat r2[3] = { C * idet, (b * i - a * j) * idet, (a * f - b * e) * idet };

		// Normal transforms by the transpose of these inverse rows.
		GLfloat px = equation[0], py = equation[1], pz = equation[2], pw = equation[3];

		clip0Plane[0] = r0[0] * px + r1[0] * py + r2[0] * pz;
		clip0Plane[1] = r0[1] * px + r1[1] * py + r2[1] * pz;
		clip0Plane[2] = r0[2] * px + r1[2] * py + r2[2] * pz;
		clip0Plane[3] = pw - (clip0Plane[0] * d + clip0Plane[1] * h + clip0Plane[2] * l);
	}
	markUniformsDirty();
}

void glFogf(GLenum pname, GLfloat param)
{
	switch (pname)
	{
	case GL_FOG_START:   fogStart = param; break;
	case GL_FOG_END:     fogEnd = param; break;
	case GL_FOG_DENSITY: fogDensity = param; break;
	case GL_FOG_MODE:    fogMode = (param == GL_LINEAR) ? 0 : (param == GL_EXP2) ? 2 : 1; break;
	default: break;
	}
	markUniformsDirty();
}

void glFogi(GLenum pname, GLint param)
{
	glFogf(pname, (GLfloat) param);
}

void glFogfv(GLenum pname, const GLfloat *params)
{
	if (pname == GL_FOG_COLOR)
	{
		memcpy(fogColor, params, sizeof(fogColor));
	}
	else if (pname == GL_FOG_START)
	{
		fogStart = params[0];
	}
	else if (pname == GL_FOG_END)
	{
		fogEnd = params[0];
	}
	else if (pname == GL_FOG_DENSITY)
	{
		fogDensity = params[0];
	}
	markUniformsDirty();
}

void glFogiv(GLenum pname, const GLint *params)
{
	if (pname == GL_FOG_COLOR)
	{
		for (int i = 0; i < 4; i++)
		{
			fogColor[i] = (GLfloat) params[i] / 255.0f;
		}
	}
	markUniformsDirty();
}

void glBegin(GLenum mode)
{
	imActive = 1;
	imMode = mode;
	imCount = 0;
}

void glEnd(void)
{
	imActive = 0;
	imFlush();
}

void glVertex2f(GLfloat x, GLfloat y)
{
	imVert(x, y, 0.0f);
}

void glVertex2fv(const GLfloat *v)
{
	imVert(v[0], v[1], 0.0f);
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
	imVert(x, y, z);
}

void glVertex3fv(const GLfloat *v)
{
	imVert(v[0], v[1], v[2]);
}

void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
	if (w != 1.0f && w != 0.0f)
	{
		x /= w; y /= w; z /= w;
	}
	imVert(x, y, z);
}

void glVertex4fv(const GLfloat *v)
{
	glVertex4f(v[0], v[1], v[2], v[3]);
}

void glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
	curColor[0] = red; curColor[1] = green; curColor[2] = blue; curColor[3] = 1.0f;
}

void glColor3fv(const GLfloat *v)
{
	curColor[0] = v[0]; curColor[1] = v[1]; curColor[2] = v[2]; curColor[3] = 1.0f;
}

void glColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
	glColor3f(red / 255.0f, green / 255.0f, blue / 255.0f);
}

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	curColor[0] = red; curColor[1] = green; curColor[2] = blue; curColor[3] = alpha;
}

void glColor4fv(const GLfloat *v)
{
	memcpy(curColor, v, sizeof(curColor));
}

void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
	glColor4f(red / 255.0f, green / 255.0f, blue / 255.0f, alpha / 255.0f);
}

void glColor4ubv(const GLubyte *v)
{
	glColor4f(v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f, v[3] / 255.0f);
}

void glTexCoord1f(GLfloat s)
{
	curTC[0][0] = s;
}

void glTexCoord2f(GLfloat s, GLfloat t)
{
	curTC[0][0] = s;
	curTC[0][1] = t;
}

void glTexCoord2fv(const GLfloat *v)
{
	curTC[0][0] = v[0];
	curTC[0][1] = v[1];
}

void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
	(void) r;
	curTC[0][0] = s;
	curTC[0][1] = t;
	(void) q;
}

void glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t)
{
	int u = target - GL_TEXTURE0_ARB;

	if (u >= 0 && u < SHIM_MAX_TMUS)
	{
		curTC[u][0] = s;
		curTC[u][1] = t;
	}
}

void glMultiTexCoord2fvARB(GLenum target, const GLfloat *v)
{
	glMultiTexCoord2fARB(target, v[0], v[1]);
}

void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
	glBegin(GL_QUADS);
	glVertex2f(x1, y1);
	glVertex2f(x2, y1);
	glVertex2f(x2, y2);
	glVertex2f(x1, y2);
	glEnd();
}

void glRasterPos3fv(const GLfloat *v)
{
	(void) v;
}

void glBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig,
              GLfloat xmove, GLfloat ymove, const GLubyte *bitmap)
{
	(void) width; (void) height; (void) xorig; (void) yorig; (void) xmove; (void) ymove; (void) bitmap;
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	if (imActive || count <= 0 || !arrVertex.enabled || !arrVertex.ptr)
	{
		return;
	}

	repackVerts(first, count);
	statDraw((size_t) count);
	drawWithCurrentProgram(mode, 0, count);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	if (imActive || count <= 0 || !arrVertex.enabled || !arrVertex.ptr)
	{
		return;
	}
	if (type != GL_UNSIGNED_INT && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_BYTE)
	{
		lastError = GL_INVALID_ENUM;
		return;
	}
	if (!indices)
	{
		lastError = GL_INVALID_VALUE;
		return;
	}

	statDraw((size_t) count);

	// resolve max index to repack exactly the referenced range
	GLuint maxIdx = 0;

	if (type == GL_UNSIGNED_INT)
	{
		const GLuint *p = (const GLuint *) indices;
		for (GLsizei i = 0; i < count; i++)
		{
			if (p[i] > maxIdx)
			{
				maxIdx = p[i];
			}
		}
	}
	else if (type == GL_UNSIGNED_SHORT)
	{
		const GLushort *p = (const GLushort *) indices;
		for (GLsizei i = 0; i < count; i++)
		{
			if (p[i] > maxIdx)
			{
				maxIdx = p[i];
			}
		}
	}
	else
	{
		const GLubyte *p = (const GLubyte *) indices;
		for (GLsizei i = 0; i < count; i++)
		{
			if (p[i] > maxIdx)
			{
				maxIdx = p[i];
			}
		}
	}

	repackVerts(0, (GLsizei) maxIdx + 1);

	// remap indices through the quad/polygon converters or draw directly
	GLenum effMode;
	convertPrimitive(mode, count, &effMode);

	if (mode == GL_QUADS || mode == GL_QUAD_STRIP || mode == GL_POLYGON)
	{
		// rebuild triangle indices referencing the repacked verts
		ensureIdx((size_t) count * 3);
		idxUsed = 0;

		if (mode == GL_QUADS)
		{
			for (GLsizei q = 0; q < count / 4; q++)
			{
				GLuint b;
				if (type == GL_UNSIGNED_INT) { b = ((const GLuint *) indices)[q * 4]; }
				else if (type == GL_UNSIGNED_SHORT) { b = ((const GLushort *) indices)[q * 4]; }
				else { b = ((const GLubyte *) indices)[q * 4]; }
				GLuint v0 = b, v1, v2, v3;
				if (type == GL_UNSIGNED_INT)
				{
					const GLuint *p = (const GLuint *) indices;
					v0 = p[q * 4]; v1 = p[q * 4 + 1]; v2 = p[q * 4 + 2]; v3 = p[q * 4 + 3];
				}
				else if (type == GL_UNSIGNED_SHORT)
				{
					const GLushort *p = (const GLushort *) indices;
					v0 = p[q * 4]; v1 = p[q * 4 + 1]; v2 = p[q * 4 + 2]; v3 = p[q * 4 + 3];
				}
				else
				{
					const GLubyte *p = (const GLubyte *) indices;
					v0 = p[q * 4]; v1 = p[q * 4 + 1]; v2 = p[q * 4 + 2]; v3 = p[q * 4 + 3];
				}
				scratchIdx[idxUsed++] = v0;
				scratchIdx[idxUsed++] = v1;
				scratchIdx[idxUsed++] = v2;
				scratchIdx[idxUsed++] = v0;
				scratchIdx[idxUsed++] = v2;
				scratchIdx[idxUsed++] = v3;
			}
		}
		else if (mode == GL_QUAD_STRIP)
		{
			for (GLsizei q = 0; q < count / 2 - 1; q++)
			{
				GLuint v0, v1, v2, v3;
				if (type == GL_UNSIGNED_INT)
				{
					const GLuint *p = (const GLuint *) indices;
					v0 = p[q * 2]; v1 = p[q * 2 + 1]; v2 = p[q * 2 + 2]; v3 = p[q * 2 + 3];
				}
				else if (type == GL_UNSIGNED_SHORT)
				{
					const GLushort *p = (const GLushort *) indices;
					v0 = p[q * 2]; v1 = p[q * 2 + 1]; v2 = p[q * 2 + 2]; v3 = p[q * 2 + 3];
				}
				else
				{
					const GLubyte *p = (const GLubyte *) indices;
					v0 = p[q * 2]; v1 = p[q * 2 + 1]; v2 = p[q * 2 + 2]; v3 = p[q * 2 + 3];
				}
				scratchIdx[idxUsed++] = v0;
				scratchIdx[idxUsed++] = v1;
				scratchIdx[idxUsed++] = v2;
				scratchIdx[idxUsed++] = v2;
				scratchIdx[idxUsed++] = v1;
				scratchIdx[idxUsed++] = v3;
			}
		}
		else // GL_POLYGON
		{
			GLuint v0;
			if (type == GL_UNSIGNED_INT) { v0 = ((const GLuint *) indices)[0]; }
			else if (type == GL_UNSIGNED_SHORT) { v0 = ((const GLushort *) indices)[0]; }
			else { v0 = ((const GLubyte *) indices)[0]; }

			for (GLsizei t = 1; t < count - 1; t++)
			{
				GLuint v1, v2;
				if (type == GL_UNSIGNED_INT)
				{
					const GLuint *p = (const GLuint *) indices;
					v1 = p[t]; v2 = p[t + 1];
				}
				else if (type == GL_UNSIGNED_SHORT)
				{
					const GLushort *p = (const GLushort *) indices;
					v1 = p[t]; v2 = p[t + 1];
				}
				else
				{
					const GLubyte *p = (const GLubyte *) indices;
					v1 = p[t]; v2 = p[t + 1];
				}
				scratchIdx[idxUsed++] = v0;
				scratchIdx[idxUsed++] = v1;
				scratchIdx[idxUsed++] = v2;
			}
		}

		mat4_t mvp;
		mat4_mul(&mvp, &projectionStack[projectionDepth], &modelviewStack[modelviewDepth]);

		uploadScratch();
		if (extProgramActive && extGLProgram)
		{
			gl3_UseProgram(extGLProgram);
			if (extUMVP != -1)
			{
				gl3_UniformMatrix4fv(extUMVP, 1, GL_FALSE, mvp.m);
			}
		}
		else
		{
			gl3_UseProgram(ffProgram);
			gl3_UniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);
			gl3_UniformMatrix4fv(uMV, 1, GL_FALSE, modelviewStack[modelviewDepth].m);
			gl3_Uniform1i(uHasTex0, (texUnits[0].enabled && texUnits[0].bound && st.texture2D[0]) ? 1 : 0);
			gl3_Uniform1i(uHasTex1, (texUnits[1].enabled && texUnits[1].bound && st.texture2D[1]) ? 1 : 0);
			gl3_Uniform1i(uMode0, texUnits[0].envMode);
			gl3_Uniform1i(uMode1, texUnits[1].envMode);
			gl3_Uniform1i(uAlphaFunc, alphaTestMode());
			gl3_Uniform1f(uAlphaRef, st.alphaRef);
			gl3_Uniform1i(uClipEnabled, clip0Enabled ? 1 : 0);
			gl3_Uniform4f(uClipPlane, clip0Plane[0], clip0Plane[1], clip0Plane[2], clip0Plane[3]);
			gl3_Uniform1i(uFogEnabled, fogEnabled ? 1 : 0);
			gl3_Uniform1i(uFogMode, fogMode);
			gl3_Uniform4f(uFogColor, fogColor[0], fogColor[1], fogColor[2], fogColor[3]);
			gl3_Uniform1f(uFogDensity, fogDensity);
			gl3_Uniform1f(uFogStart, fogStart);
			gl3_Uniform1f(uFogEnd, fogEnd);
		}

		gl3_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, vboIdx);
		gl3_BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idxUsed * sizeof(GLuint)), scratchIdx, GL_STREAM_DRAW);
		gl3_DrawElements(GL_TRIANGLES, (GLsizei) idxUsed, GL_UNSIGNED_INT, 0);
		return;
	}

	// direct triangle modes: upload engine indices as-is
	drawWithCurrentProgramIndexed(mode, count, type, indices);
}

void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count,
                         GLenum type, const GLvoid *indices)
{
	(void) start;
	(void) end;
	glDrawElements(mode, count, type, indices);
}

void glEnableClientState(GLenum cap)
{
	switch (cap)
	{
	case GL_VERTEX_ARRAY:        arrVertex.enabled = GL_TRUE; break;
	case GL_COLOR_ARRAY:         arrColor.enabled = GL_TRUE; break;
	case GL_TEXTURE_COORD_ARRAY: arrTexCoord[clientTMU].enabled = GL_TRUE; break;
	default: break;
	}
}

void glDisableClientState(GLenum cap)
{
	switch (cap)
	{
	case GL_VERTEX_ARRAY:        arrVertex.enabled = GL_FALSE; break;
	case GL_COLOR_ARRAY:         arrColor.enabled = GL_FALSE; break;
	case GL_TEXTURE_COORD_ARRAY: arrTexCoord[clientTMU].enabled = GL_FALSE; break;
	default: break;
	}
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
	arrVertex.size = size;
	arrVertex.type = type;
	arrVertex.stride = stride;
	arrVertex.ptr = ptr;
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
	arrColor.size = size;
	arrColor.type = type;
	arrColor.stride = stride;
	arrColor.ptr = ptr;
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
	arrTexCoord[clientTMU].size = size;
	arrTexCoord[clientTMU].type = type;
	arrTexCoord[clientTMU].stride = stride;
	arrTexCoord[clientTMU].ptr = ptr;
}

void glIndexPointer(GLenum type, GLsizei stride, const GLvoid *ptr)
{
	(void) type; (void) stride; (void) ptr;
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr)
{
	(void) type; (void) stride; (void) ptr;
}

void glLockArraysEXT(GLint first, GLsizei count)
{
	(void) first;
	(void) count;
}

void glUnlockArraysEXT(void)
{
}

void glGenTextures(GLsizei n, GLuint *textures)
{
	if (n < 0) { lastError = GL_INVALID_VALUE; return; }
	for (GLsizei i = 0; i < n; i++)
	{
		textures[i] = 0;
		for (int attempt = 0; attempt < SHIM_MAX_TEXTURES - 1; attempt++)
		{
			texNameCount = texNameCount % (SHIM_MAX_TEXTURES - 1) + 1;
			if (!texNameToGL[texNameCount])
			{
				gl3_GenTextures(1, &texNameToGL[texNameCount]);
				if (texNameToGL[texNameCount]) textures[i] = (GLuint) texNameCount;
				break;
			}
		}
		if (!textures[i]) lastError = GL_OUT_OF_MEMORY;
	}
}

void glDeleteTextures(GLsizei n, const GLuint *textures)
{
	if (n < 0) { lastError = GL_INVALID_VALUE; return; }
	for (GLsizei i = 0; i < n; i++)
	{
		GLuint name = textures[i];
		if (!name || name >= SHIM_MAX_TEXTURES || !texNameToGL[name]) continue;
		gl3_DeleteTextures(1, &texNameToGL[name]);
		texNameToGL[name] = 0;
		texLuminanceMode[name] = 0;
		for (int unit = 0; unit < SHIM_MAX_TMUS; unit++)
			if (texUnits[unit].bound == name) texUnits[unit].bound = 0;
	}
	markUniformsDirty();
}

void glBindTexture(GLenum target, GLuint texture)
{
	if (curTMU < SHIM_MAX_TMUS)
	{
		texUnits[curTMU].bound = (target == GL_TEXTURE_2D) ? texture : 0;
		markUniformsDirty();
	}

	GLuint real = (texture > 0 && texture < SHIM_MAX_TEXTURES) ? texNameToGL[texture] : 0;

	gl3_BindTexture(target, real);
}

typedef struct
{
	GLint rowLength, skipRows, skipPixels, alignment;
	GLubyte *pixels;
} luminanceUpload_t;

static int luminanceMode(GLint format)
{
	if (format == 1 || format == GL_LUMINANCE || format == GL_LUMINANCE8 || format == GL_LUMINANCE16) return 1;
	if (format == 2 || format == GL_LUMINANCE_ALPHA || format == GL_LUMINANCE8_ALPHA8 || format == GL_LUMINANCE16_ALPHA16) return 2;
	return 0;
}

static int prepareLuminanceUpload(luminanceUpload_t *upload, int mode,
                                 GLsizei width, GLsizei height, const GLvoid *data)
{
	size_t rowBytes, stride;
	const GLubyte *source;
	memset(upload, 0, sizeof(*upload));
	if (width < 0 || height < 0 || (height > 0 && (size_t)width > SIZE_MAX / 4 / (size_t)height))
	{
		lastError = GL_INVALID_VALUE;
		return 0;
	}
	if (!data || !width || !height) return 1;
	upload->pixels = malloc((size_t)width * height * 4);
	if (!upload->pixels) { lastError = GL_OUT_OF_MEMORY; return 0; }
	gl3_GetIntegerv(GL_UNPACK_ROW_LENGTH, &upload->rowLength);
	gl3_GetIntegerv(GL_UNPACK_SKIP_ROWS, &upload->skipRows);
	gl3_GetIntegerv(GL_UNPACK_SKIP_PIXELS, &upload->skipPixels);
	gl3_GetIntegerv(GL_UNPACK_ALIGNMENT, &upload->alignment);
	rowBytes = (size_t)(upload->rowLength ? upload->rowLength : width) * 4;
	stride = (rowBytes + upload->alignment - 1) / upload->alignment * upload->alignment;
	source = (const GLubyte *)data + (size_t)upload->skipRows * stride + (size_t)upload->skipPixels * 4;
	for (GLsizei y = 0; y < height; y++)
	{
		for (GLsizei x = 0; x < width; x++)
		{
			const GLubyte *input = source + (size_t)y * stride + (size_t)x * 4;
			GLubyte *output = upload->pixels + ((size_t)y * width + x) * 4;
			output[0] = output[1] = output[2] = input[0];
			output[3] = mode == 2 ? input[3] : 255;
		}
	}
	gl3_PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	gl3_PixelStorei(GL_UNPACK_SKIP_ROWS, 0);
	gl3_PixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	gl3_PixelStorei(GL_UNPACK_ALIGNMENT, 1);
	return 1;
}

static void finishLuminanceUpload(luminanceUpload_t *upload)
{
	if (!upload->pixels) return;
	gl3_PixelStorei(GL_UNPACK_ROW_LENGTH, upload->rowLength);
	gl3_PixelStorei(GL_UNPACK_SKIP_ROWS, upload->skipRows);
	gl3_PixelStorei(GL_UNPACK_SKIP_PIXELS, upload->skipPixels);
	gl3_PixelStorei(GL_UNPACK_ALIGNMENT, upload->alignment);
	free(upload->pixels);
}

void glTexImage2D(GLenum target, GLint level, GLint internalFormat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const GLvoid *data)
{
	if (target != GL_TEXTURE_2D)
	{
		if (target == GL_PROXY_TEXTURE_2D || target == GL_PROXY_TEXTURE_1D)
		{
			return; // pretend success
		}
		return;
	}

	GLint ifmt = internalFormat;
	int mode = luminanceMode(ifmt);
	luminanceUpload_t upload = {0};
	if (mode && format == GL_RGBA && type == GL_UNSIGNED_BYTE)
	{
		if (!prepareLuminanceUpload(&upload, mode, width, height, data)) return;
		ifmt = GL_RGBA8;
		if (upload.pixels) data = upload.pixels;
	}
	else mode = 0;

	// normalize legacy/unsized formats for WebGL2
	if (ifmt == 1 || ifmt == GL_LUMINANCE) { ifmt = GL_LUMINANCE8; }
	else if (ifmt == 2 || ifmt == GL_LUMINANCE_ALPHA) { ifmt = GL_LUMINANCE8_ALPHA8; }
	else if (ifmt == 3 || ifmt == GL_RGB || ifmt == GL_RGB5 || ifmt == GL_RGB4) { ifmt = GL_RGB8; }
	else if (ifmt == 4 || ifmt == GL_RGBA || ifmt == GL_RGBA4 || ifmt == GL_RGB5_A1 || ifmt == GL_RGBA2) { ifmt = GL_RGBA8; }
	else if (ifmt == GL_ALPHA) { ifmt = GL_ALPHA8; }
	else if (ifmt == GL_INTENSITY || ifmt == GL_INTENSITY8) { ifmt = GL_LUMINANCE8; }
	else if (ifmt == GL_R3_G3_B2) { ifmt = GL_RGB8; }
	// ET supplies RGBA pixels even for opaque RGB storage. WebGL2 requires
	// the sized storage format to match the upload's base format.
	if (format == GL_RGBA && ifmt == GL_RGB8) { ifmt = GL_RGBA8; }
	else if (format == GL_RGB && ifmt == GL_RGBA8) { ifmt = GL_RGB8; }

	// depth textures: WebGL2 only accepts specific format/type pairs.
	// The engine uploads DEPTH_COMPONENT32 with GL_FLOAT (legal on desktop
	// GL, illegal in ES3) - remap to the float-capable DEPTH_COMPONENT32F,
	// and force integer types for the fixed-point depth formats.
	if (format == GL_DEPTH_COMPONENT)
	{
		if (type == GL_FLOAT)
		{
			ifmt = GL_DEPTH_COMPONENT32F;
		}
		else
		{
			ifmt = GL_DEPTH_COMPONENT24;
			type = GL_UNSIGNED_INT;
		}
	}

	gl3_TexImage2D(GL_TEXTURE_2D, level, ifmt, width, height, 0, format, type, data);
	finishLuminanceUpload(&upload);
	if (level == 0 && texUnits[curTMU].bound < SHIM_MAX_TEXTURES)
		texLuminanceMode[texUnits[curTMU].bound] = mode;
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const GLvoid *data)
{
	luminanceUpload_t upload = {0};
	GLuint name = texUnits[curTMU].bound;
	if (target == GL_TEXTURE_2D && name < SHIM_MAX_TEXTURES && texLuminanceMode[name] && format == GL_RGBA && type == GL_UNSIGNED_BYTE)
	{
		if (!prepareLuminanceUpload(&upload, texLuminanceMode[name], width, height, data)) return;
		if (upload.pixels) data = upload.pixels;
	}
	gl3_TexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, data);
	finishLuminanceUpload(&upload);
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalFormat,
                      GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
	GLint ifmt = internalFormat;

	if (ifmt == GL_RGB) { ifmt = GL_RGB8; }
	else if (ifmt == GL_RGBA || ifmt == GL_RGBA4 || ifmt == GL_RGB5_A1) { ifmt = GL_RGBA8; }
	else if (ifmt == GL_LUMINANCE) { ifmt = GL_LUMINANCE8; }
	else if (ifmt == GL_LUMINANCE_ALPHA) { ifmt = GL_LUMINANCE8_ALPHA8; }

	gl3_CopyTexImage2D(target, level, ifmt, x, y, width, height, border);
	if (target == GL_TEXTURE_2D && level == 0 && texUnits[curTMU].bound < SHIM_MAX_TEXTURES)
		texLuminanceMode[texUnits[curTMU].bound] = 0;
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                         GLint x, GLint y, GLsizei width, GLsizei height)
{
	gl3_CopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
	gl3_TexParameterf(target, pname, param);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
	gl3_TexParameteri(target, pname, param);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
	if (pname == GL_TEXTURE_BORDER_COLOR)
	{
		return;
	}
	gl3_TexParameterf(target, pname, params[0]);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
	if (pname == GL_TEXTURE_BORDER_COLOR)
	{
		return;
	}
	gl3_TexParameteri(target, pname, params[0]);
}

void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
	glTexEnvi(target, pname, (GLint) param);
}

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
	if (target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE)
	{
		return;
	}

	if (curTMU < SHIM_MAX_TMUS)
	{
		texUnits[curTMU].envMode = texenvFromGL((GLenum) param);
		markUniformsDirty();
	}
}

void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
	if (pname == GL_TEXTURE_ENV_MODE)
	{
		glTexEnvf(target, pname, params[0]);
	}
}

void glTexEnviv(GLenum target, GLenum pname, const GLint *params)
{
	if (pname == GL_TEXTURE_ENV_MODE)
	{
		glTexEnvi(target, pname, params[0]);
	}
}

void glPixelStorei(GLenum pname, GLint param)
{
	gl3_PixelStorei(pname, param);
}

void glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
	(void) n; (void) textures; (void) priorities;
}

void glActiveTextureARB(GLenum texture)
{
	curTMU = texture - GL_TEXTURE0_ARB;
	if (curTMU < 0 || curTMU >= SHIM_MAX_TMUS)
	{
		curTMU = 0;
	}
	gl3_ActiveTexture(GL_TEXTURE0 + curTMU);
}

void glClientActiveTextureARB(GLenum texture)
{
	clientTMU = texture - GL_TEXTURE0_ARB;
	if (clientTMU < 0 || clientTMU >= SHIM_MAX_TMUS)
	{
		clientTMU = 0;
	}
}

GLuint glGenLists(GLsizei range)
{
	(void) range;
	return ++listBase;
}

void glDeleteLists(GLuint list, GLsizei range)
{
	(void) list;
	(void) range;
}

void glNewList(GLuint list, GLenum mode)
{
	(void) list;
	(void) mode;
}

void glEndList(void)
{
}

void glCallList(GLuint list)
{
	(void) list;
}

void glCallLists(GLsizei n, GLenum type, const GLvoid *lists)
{
	(void) n; (void) type; (void) lists;
}

void glListBase(GLuint base)
{
	listBase = base;
}

GLboolean glIsList(GLuint list)
{
	(void) list;
	return GL_FALSE;
}

GLboolean glIsTexture(GLuint texture)
{
	GLuint real = (texture > 0 && texture < SHIM_MAX_TEXTURES) ? texNameToGL[texture] : 0;

	return real ? gl3_IsTexture(real) : GL_FALSE;
}

void glReadBuffer(GLenum mode)
{
	(void) mode;
}

void glDrawBuffer(GLenum mode)
{
	(void) mode;
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, GLvoid *data)
{
	gl3_ReadPixels(x, y, width, height, format, type, data);
}

// framebuffer objects (1:1 with WebGL2)
void glGenFramebuffersEXT(GLsizei n, GLuint *framebuffers)
{
	gl3_GenFramebuffers(n, framebuffers);
}

void glDeleteFramebuffersEXT(GLsizei n, const GLuint *framebuffers)
{
	gl3_DeleteFramebuffers(n, framebuffers);
}

void glBindFramebufferEXT(GLenum target, GLuint framebuffer)
{
	gl3_BindFramebuffer(target, framebuffer);
}

void glGenRenderbuffersEXT(GLsizei n, GLuint *renderbuffers)
{
	gl3_GenRenderbuffers(n, renderbuffers);
}

void glDeleteRenderbuffersEXT(GLsizei n, const GLuint *renderbuffers)
{
	gl3_DeleteRenderbuffers(n, renderbuffers);
}

void glBindRenderbufferEXT(GLenum target, GLuint renderbuffer)
{
	gl3_BindRenderbuffer(target, renderbuffer);
}

void glRenderbufferStorageEXT(GLenum target, GLenum internalFormat,
                              GLsizei width, GLsizei height)
{
	// DEPTH_COMPONENT32 is a desktop-GL texture format, not a valid ES3
	// renderbuffer format - remap to the WebGL2-supported DEPTH_COMPONENT24
	if (internalFormat == GL_DEPTH_COMPONENT32)
	{
		internalFormat = GL_DEPTH_COMPONENT24;
	}
	gl3_RenderbufferStorage(target, internalFormat, width, height);
}

void glRenderbufferStorageMultisampleEXT(GLenum target, GLsizei samples,
                                         GLenum internalFormat, GLsizei width, GLsizei height)
{
	if (internalFormat == GL_DEPTH_COMPONENT32)
	{
		internalFormat = GL_DEPTH_COMPONENT24;
	}
	gl3_RenderbufferStorageMultisample(target, samples, internalFormat, width, height);
}

void glFramebufferRenderbufferEXT(GLenum target, GLenum attachment,
                                  GLenum renderbuffertarget, GLuint renderbuffer)
{
	gl3_FramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
}

void glFramebufferTexture2DEXT(GLenum target, GLenum attachment, GLenum textarget,
                               GLuint texture, GLint level)
{
	GLuint real = (texture > 0 && texture < SHIM_MAX_TEXTURES) ? texNameToGL[texture] : texture;

	gl3_FramebufferTexture2D(target, attachment, textarget, real, level);
}

// the engine mixes EXT and core names; both must go through the name mapping
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                            GLuint texture, GLint level)
{
	glFramebufferTexture2DEXT(target, attachment, textarget, texture, level);
}

GLenum glCheckFramebufferStatusEXT(GLenum target)
{
	GLenum s = gl3_CheckFramebufferStatus(target);

	if (s != GL_FRAMEBUFFER_COMPLETE_EXT)
	{
		GLint colorType = -1, colorName = -1, depthType = -1, depthName = -1, dsType = -1, dsName = -1;

		gl3_GetFramebufferAttachmentParameteriv(target, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorType);
		gl3_GetFramebufferAttachmentParameteriv(target, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &colorName);
		gl3_GetFramebufferAttachmentParameteriv(target, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depthType);
		gl3_GetFramebufferAttachmentParameteriv(target, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depthName);
		gl3_GetFramebufferAttachmentParameteriv(target, GL_DEPTH_STENCIL_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &dsType);
		gl3_GetFramebufferAttachmentParameteriv(target, GL_DEPTH_STENCIL_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &dsName);
		fprintf(stderr, "ETWebGL: FBO INCOMPLETE status=%x color0(type=%d name=%d) depth(type=%d name=%d) depthstencil(type=%d name=%d) err=%x\n",
		        s, colorType, colorName, depthType, depthName, dsType, dsName, gl3_GetError());
	}

	return s;
}

void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter)
{
	gl3_BlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}

void glBlitFramebufferEXT(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                          GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                          GLbitfield mask, GLenum filter)
{
	glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}

void glGenerateMipmapEXT(GLenum target)
{
	gl3_GenerateMipmap(target);
}

void glUniform1f(GLint location, GLfloat v0)
{
	gl3_Uniform1f(location, v0);
}

void glUniform1i(GLint location, GLint v0)
{
	gl3_Uniform1i(location, v0);
}

void glUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
	gl3_Uniform2f(location, v0, v1);
}

void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
	gl3_Uniform3f(location, v0, v1, v2);
}

void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
	gl3_Uniform4f(location, v0, v1, v2, v3);
}

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
	gl3_UniformMatrix4fv(location, count, transpose, value);
}

// ---------------------------------------------------------------------------
// glGet* / strings / GLEW emulation
// ---------------------------------------------------------------------------
static const char *shimExtensions =
	"GL_ARB_multitexture "
	"GL_EXT_compiled_vertex_array "
	"GL_EXT_texture_env_add "
	"GL_ARB_texture_non_power_of_two "
	"GL_ARB_fragment_program "
	"GL_ARB_framebuffer_object "
	"GL_EXT_framebuffer_multisample "
	"GL_EXT_texture_filter_anisotropic "
	"GL_ARB_depth_texture ";
static const char *shimBaseExtensions =
	"GL_ARB_multitexture GL_EXT_compiled_vertex_array GL_EXT_texture_env_add "
	"GL_ARB_texture_non_power_of_two GL_ARB_fragment_program GL_ARB_framebuffer_object "
	"GL_EXT_framebuffer_multisample GL_ARB_depth_texture ";

int GLEW_ARB_multitexture = 0;
int GLEW_ARB_texture_compression = 0;
int GLEW_ARB_texture_non_power_of_two = 0;
int GLEW_ARB_fragment_program = 0;
int GLEW_ARB_framebuffer_object = 0;
int GLEW_ARB_depth_texture = 0;
int GLEW_EXT_compiled_vertex_array = 0;
int GLEW_EXT_texture_env_add = 0;
int GLEW_EXT_texture_compression_s3tc = 0;
int GLEW_EXT_texture_filter_anisotropic = 0;
int GLEW_EXT_framebuffer_multisample = 0;
int GLEW_S3_s3tc = 0;

const GLubyte *glewGetString(GLenum name)
{
	(void) name;
	return (const GLubyte *) GLEW_VERSION;
}

const GLubyte *glewGetErrorString(GLenum error)
{
	(void) error;
	return (const GLubyte *) "no error";
}

GLboolean glewIsSupported(const char *name)
{
	const char *extensions = GLEW_EXT_texture_filter_anisotropic ? shimExtensions : shimBaseExtensions;
	return strstr(extensions, name) ? GL_TRUE : GL_FALSE;
}

const GLubyte *glGetString(GLenum name)
{
	switch (name)
	{
	case GL_VENDOR:    return (const GLubyte *) "ETWebGL";
	case GL_RENDERER:  return (const GLubyte *) "WebGL2 fixed-function emulator";
	case GL_VERSION:   return (const GLubyte *) "1.1 ETWebGL2 (WebGL 2.0)";
	case GL_EXTENSIONS: return (const GLubyte *) (GLEW_EXT_texture_filter_anisotropic ? shimExtensions : shimBaseExtensions);
	default:           return (const GLubyte *) "";
	}
}

const GLubyte *glGetStringi(GLenum name, GLuint index)
{
	static const char *extList[] =
	{
		"GL_ARB_multitexture",
		"GL_EXT_compiled_vertex_array",
		"GL_EXT_texture_env_add",
		"GL_ARB_texture_non_power_of_two",
		"GL_ARB_fragment_program",
		"GL_ARB_framebuffer_object",
		"GL_EXT_framebuffer_multisample",
		"GL_EXT_texture_filter_anisotropic",
		"GL_ARB_depth_texture"
	};

	(void) name;

	if (index < (GLuint)(8 + !!GLEW_EXT_texture_filter_anisotropic))
	{
		if (!GLEW_EXT_texture_filter_anisotropic && index >= 7) index++;
		return (const GLubyte *) extList[index];
	}
	return (const GLubyte *) "";
}

void glGetBooleanv(GLenum pname, GLboolean *params)
{
	switch (pname)
	{
	case GL_DEPTH_WRITEMASK: *params = st.depthMask; break;
	case GL_COLOR_WRITEMASK:
		params[0] = st.colorMaskR; params[1] = st.colorMaskG;
		params[2] = st.colorMaskB; params[3] = st.colorMaskA;
		break;
	default:
		params[0] = 0;
		break;
	}
}

void glGetIntegerv(GLenum pname, GLint *params)
{
	switch (pname)
	{
	case GL_VIEWPORT:
		params[0] = vpX; params[1] = vpY; params[2] = vpW; params[3] = vpH;
		break;
	case GL_MAX_TEXTURE_SIZE:           gl3_GetIntegerv(pname, params); break;
	case GL_MAX_TEXTURE_UNITS_ARB:
	case GL_MAX_TEXTURE_IMAGE_UNITS:    *params = 2; break;
	case GL_MAX_RENDERBUFFER_SIZE_EXT:  gl3_GetIntegerv(pname, params); break;
	case GL_MAX_COLOR_ATTACHMENTS_EXT:  *params = 1; break;
	case GL_MAX_SAMPLES_EXT:            gl3_GetIntegerv(pname, params); break;
	case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT:
		if (GLEW_EXT_texture_filter_anisotropic) gl3_GetIntegerv(pname, params);
		else *params = 1;
		break;
	case GL_DEPTH_BITS:
	case GL_STENCIL_BITS:               gl3_GetIntegerv(pname, params); break;
	case GL_NUM_EXTENSIONS:             *params = 8 + !!GLEW_EXT_texture_filter_anisotropic; break;
	case GL_NUM_COMPRESSED_TEXTURE_FORMATS: *params = 0; break;
	case GL_ACTIVE_TEXTURE:             *params = GL_TEXTURE0_ARB + curTMU; break;
	case GL_CLIENT_ACTIVE_TEXTURE:      *params = GL_TEXTURE0_ARB + clientTMU; break;
	case GL_TEXTURE_BINDING_2D:         *params = texUnits[curTMU].bound; break;
	case GL_DRAW_BUFFER:                *params = GL_BACK; break;
	case GL_READ_BUFFER:                *params = GL_BACK; break;
	case GL_FRAMEBUFFER_BINDING_EXT:    *params = 0; gl3_GetIntegerv(GL_FRAMEBUFFER_BINDING, params); break;
	case GL_DRAW_FRAMEBUFFER_BINDING_EXT: gl3_GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, params); break;
	case GL_READ_FRAMEBUFFER_BINDING_EXT: gl3_GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, params); break;
	case GL_MATRIX_MODE:                *params = curMatrixMode; break;
	case GL_ALPHA_TEST_FUNC:            *params = st.alphaFunc; break;
	case GL_DEPTH_FUNC:                 *params = st.depthFunc; break;
	case GL_BLEND_SRC:                  *params = st.sfSrc; break;
	case GL_BLEND_DST:                  *params = st.sfDst; break;
	case GL_CULL_FACE_MODE:             *params = st.cullMode; break;
	case GL_FRONT_FACE:                 *params = st.frontFace; break;
	case GL_SCISSOR_BOX:
		params[0] = st.scissorX; params[1] = st.scissorY;
		params[2] = st.scissorW; params[3] = st.scissorH;
		break;
	default: *params = 0; break;
	}
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
	switch (pname)
	{
	case GL_MODELVIEW_MATRIX:
		memcpy(params, modelviewStack[modelviewDepth].m, 16 * sizeof(GLfloat));
		break;
	case GL_PROJECTION_MATRIX:
		memcpy(params, projectionStack[projectionDepth].m, 16 * sizeof(GLfloat));
		break;
	case GL_TEXTURE_MATRIX:
		memcpy(params, textureStack[curTMU][textureDepth[curTMU]].m, 16 * sizeof(GLfloat));
		break;
	case GL_COLOR_CLEAR_VALUE:
		memcpy(params, st.clearColor, 4 * sizeof(GLfloat));
		break;
	case GL_CURRENT_COLOR:
		memcpy(params, curColor, 4 * sizeof(GLfloat));
		break;
	case GL_FOG_COLOR:
		memcpy(params, fogColor, 4 * sizeof(GLfloat));
		break;
	case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT:
		if (GLEW_EXT_texture_filter_anisotropic) gl3_GetFloatv(pname, params);
		else *params = 1.0f;
		break;
	case GL_DEPTH_RANGE: params[0] = depthNear; params[1] = depthFar; break;
	case GL_ALPHA_TEST_REF: *params = st.alphaRef; break;
	case GL_POLYGON_OFFSET_FACTOR: *params = st.polygonOffsetFactor; break;
	case GL_POLYGON_OFFSET_UNITS:  *params = st.polygonOffsetUnits; break;
	default: *params = 0.0f; break;
	}
}

void glGetDoublev(GLenum pname, GLdouble *params)
{
	GLfloat f[16];
	int count = 1;
	if (pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX || pname == GL_TEXTURE_MATRIX) count = 16;
	else if (pname == GL_CURRENT_COLOR || pname == GL_COLOR_CLEAR_VALUE || pname == GL_FOG_COLOR) count = 4;
	else if (pname == GL_DEPTH_RANGE) count = 2;

	glGetFloatv(pname, f);
	for (int i = 0; i < count; i++)
	{
		params[i] = f[i];
	}
}

GLenum glewInit(void)
{
	static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE shimContext = 0;
	GLint viewport[4];
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_get_current_context();

	if (context != shimContext)
	{
		// SDL creates a new context on vid_restart. GL names from the old
		// context cannot be reused, including the shim's private draw buffers.
		shimContext = context;
		buffersReady = 0;
		texNameCount = 0;
		memset(texNameToGL, 0, sizeof(texNameToGL));
		memset(texLuminanceMode, 0, sizeof(texLuminanceMode));
		memset(texUnits, 0, sizeof(texUnits));
		for (int i = 0; i < ARB_MAX_SHADERS; i++) free(arbShaders[i].source);
		memset(arbPrograms, 0, sizeof(arbPrograms));
		memset(arbShaders, 0, sizeof(arbShaders));
		extProgramActive = 0;
		extGLProgram = 0;
		modelviewDepth = projectionDepth = 0;
		curTMU = clientTMU = 0;
		attribStackDepth = 0;
		fogEnabled = clip0Enabled = imActive = 0;
		imCount = 0;
		curMatrixMode = GL_MODELVIEW;
		markUniformsDirty();
	}
	if (!buffersReady)
	{
		memset(&st, 0, sizeof(st));
		st.depthMask = 1;
		st.colorMaskR = st.colorMaskG = st.colorMaskB = st.colorMaskA = 1;
		st.alphaFunc = GL_ALWAYS;
		st.depthFunc = GL_LESS;
		st.cullMode = GL_BACK;
		st.frontFace = GL_CCW;
		st.stencilFunc = GL_ALWAYS;
		st.stencilMask = 0xFFFFFFFF;
		st.stencilValueMask = 0xFFFFFFFF;
		st.clearDepth = 1.0;

		for (int i = 0; i < SHIM_MAX_TMUS; i++)
		{
			texUnits[i].envMode = TEXENV_MODULATE;
			mat4_identity(&textureStack[i][0]);
			textureDepth[i] = 0;
		}

		mat4_identity(&modelviewStack[0]);
		mat4_identity(&projectionStack[0]);

		memset(&arrVertex, 0, sizeof(arrVertex));
		memset(&arrColor, 0, sizeof(arrColor));
		for (int i = 0; i < SHIM_MAX_TMUS; i++)
		{
			memset(&arrTexCoord[i], 0, sizeof(arrTexCoord[i]));
		}
		for (int i = 0; i < 4; i++) curColor[i] = 1.0f;
		memset(curTC, 0, sizeof(curTC));
		for (int i = 0; i < SHIM_MAX_TMUS; i++) curTC[i][3] = 1.0f;
		fogMode = 1; fogStart = 0.0f; fogEnd = fogDensity = 1.0f;
		memset(fogColor, 0, sizeof(fogColor));
		memset(clip0Plane, 0, sizeof(clip0Plane));
		depthNear = 0.0f; depthFar = 1.0f;
		lastError = GL_NO_ERROR;
		gl3_GetIntegerv(GL_VIEWPORT, viewport);
		vpX = viewport[0]; vpY = viewport[1]; vpW = viewport[2]; vpH = viewport[3];
		st.scissorW = vpW; st.scissorH = vpH;

		ffProgram = buildFFProgram();

		gl3_GenVertexArrays(1, &vaoMain);
		gl3_BindVertexArray(vaoMain);
		gl3_GenBuffers(1, &vboVerts);
		gl3_GenBuffers(1, &vboIdx);
		gl3_BindBuffer(GL_ARRAY_BUFFER, vboVerts);
		gl3_EnableVertexAttribArray(0);
		gl3_EnableVertexAttribArray(1);
		gl3_EnableVertexAttribArray(2);
		gl3_EnableVertexAttribArray(3);
		gl3_VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 44, (const void *) 0);
		gl3_VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 44, (const void *) 12);
		gl3_VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 44, (const void *) 28);
		gl3_VertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 44, (const void *) 36);

		buffersReady = 1;
	}

	GLEW_ARB_multitexture = 1;
	GLEW_ARB_texture_non_power_of_two = 1;
	GLEW_ARB_fragment_program = 1;
	GLEW_ARB_framebuffer_object = 1;
	GLEW_ARB_depth_texture = 1;
	GLEW_EXT_compiled_vertex_array = 1;
	GLEW_EXT_texture_env_add = 1;
	GLEW_EXT_texture_filter_anisotropic = context > 0 &&
		emscripten_webgl_enable_extension(context, "EXT_texture_filter_anisotropic");
	GLEW_EXT_framebuffer_multisample = 1;
	// S3TC deliberately disabled -> engine uses uncompressed uploads

	return GLEW_OK;
}
