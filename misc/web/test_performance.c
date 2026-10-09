/* Real shim upload and browser pacing regressions; run with emcc + Node. */
#include "../../src/webgl/webgl_shim.c"
#include "../../src/qcommon/web_frame.h"
#include <assert.h>

static const void *uploadedIndices;
static GLsizeiptr uploadedBytes;
static GLenum drawnType;
static int uploads, draws;
void emscripten_glBindBuffer(GLenum target, GLuint buffer) {}
void emscripten_glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
	if (target == GL_ELEMENT_ARRAY_BUFFER) { uploadedIndices = data; uploadedBytes = size; uploads++; }
}
void emscripten_glUseProgram(GLuint program) {}
void emscripten_glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {}
void emscripten_glUniform1i(GLint location, GLint value) {}
void emscripten_glUniform1f(GLint location, GLfloat value) {}
void emscripten_glUniform4f(GLint location, GLfloat a, GLfloat b, GLfloat c, GLfloat d) {}
void emscripten_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *offset)
{
	assert(mode == GL_TRIANGLES && count == 6 && offset == 0);
	drawnType = type; draws++;
}
GLenum emscripten_glGetError(void) { return GL_NO_ERROR; }

static int countFrames(int fps, int refresh)
{
	webFrameClock_t clock = {0};
	int frames = 0;
	for (int i = 0; i < refresh * 10; i++)
		frames += Web_FrameDue(&clock, (int64_t)i * 1000000 / refresh, Web_FramePeriod(fps));
	return frames;
}
int main(void)
{
	const GLuint ints[6] = {0,1,2,0,2,3};
	const GLushort shorts[6] = {0,1,2,0,2,3};
	const GLubyte bytes[6] = {0,1,2,0,2,3};
	const void *sources[] = {ints, shorts, bytes};
	const GLenum types[] = {GL_UNSIGNED_INT, GL_UNSIGNED_SHORT, GL_UNSIGNED_BYTE};
	const int sizes[] = {24,12,6};
	const GLfloat verts[4][3] = {{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
	glVertexPointer(3, GL_FLOAT, 0, verts); glEnableClientState(GL_VERTEX_ARRAY);
	for (int i = 0; i < 3; i++)
	{
		glDrawElements(GL_TRIANGLES, 6, types[i], sources[i]);
		assert(uploadedIndices == sources[i] && uploadedBytes == sizes[i] && drawnType == types[i]);
	}
	assert(uploads == 3 && draws == 3 && !idxCap);
	glDrawElements(GL_TRIANGLES, 6, GL_FLOAT, ints);
	assert(glGetError() == GL_INVALID_ENUM && uploads == 3);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
	assert(glGetError() == GL_INVALID_VALUE && uploads == 3);
	assert(countFrames(60,60) == 600);
	assert(countFrames(60,144) == 600);
	assert(countFrames(125,144) == 1250);
	assert(countFrames(76,144) == 760);
	assert(countFrames(30,60) == 300);
	assert(countFrames(30,144) == 300);
	assert(countFrames(125,60) == 600);
	assert(countFrames(0,144) == 1440);
	assert(countFrames(1,60) == 10);
	assert(countFrames(10,60) == 100);
	assert(Web_FramePeriod(-1) == 1000);
	webFrameClock_t jitter = {0};
	for (int i = 0; i < 600; i++)
		assert(Web_FrameDue(&jitter, (int64_t)i * 1000000 / 60 + (i % 2 ? -200 : 200), 16666));
	webFrameClock_t clock = {0};
	assert(Web_FrameDue(&clock,0,33333));
	assert(!Web_FrameDue(&clock,16666,33333));
	assert(Web_FrameDue(&clock,16666,16666)); // changed cap applies immediately
	assert(Web_FrameDue(&clock,5000000,16666));
	assert(!Web_FrameDue(&clock,5000001,16666)); // no burst after tab suspension
	puts("Performance: direct index uploads (24/12/6 bytes, zero index scratch allocations), 60/144 Hz pacing, cap changes and suspension recovery passed.");
	return 0;
}
