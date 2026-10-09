/* Real shim upload and browser pacing regressions; run with emcc + Node. */
#include "../../src/webgl/webgl_shim.c"
#include "../../src/qcommon/web_frame.h"
#include <assert.h>
#include <emscripten.h>

static void checkRepacking(void)
{
	struct vertex { GLfloat xyz[4]; GLubyte rgba[4]; GLfloat uv[3], lm[2]; } vertices[8];
	for (int i = 0; i < 8; i++)
	{
		for (int j = 0; j < 4; j++) { vertices[i].xyz[j] = i * 4 + j; vertices[i].rgba[j] = i * 31 + j; }
		for (int j = 0; j < 3; j++) vertices[i].uv[j] = i * 0.125f + j;
		for (int j = 0; j < 2; j++) vertices[i].lm[j] = i * 0.25f + j;
	}
	glVertexPointer(4, GL_FLOAT, sizeof(vertices[0]), vertices[0].xyz);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(vertices[0]), vertices[0].rgba);
	glClientActiveTextureARB(GL_TEXTURE0); glTexCoordPointer(3, GL_FLOAT, sizeof(vertices[0]), vertices[0].uv);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
	glClientActiveTextureARB(GL_TEXTURE1); glTexCoordPointer(2, GL_FLOAT, sizeof(vertices[0]), vertices[0].lm);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	for (int enabled = 0; enabled < 8; enabled++)
	{
		arrColor.enabled = enabled & 1; arrTexCoord[1].enabled = enabled & 2; arrTexCoord[0].enabled = enabled & 4;
		repackVerts(2, 4); assert(scratchUsed == 44);
		for (int i = 0; i < 4; i++)
		{
			GLfloat *out = scratchVerts + i * 11;
			for (int j = 0; j < 3; j++) assert(out[j] == vertices[i + 2].xyz[j]);
			for (int j = 0; j < 4; j++) assert(out[j + 3] == (enabled & 1 ? vertices[i + 2].rgba[j] / 255.0f : curColor[j]));
			for (int j = 0; j < 2; j++) { assert(out[j + 7] == (enabled & 4 ? vertices[i + 2].uv[j] : curTC[0][j])); assert(out[j + 9] == (enabled & 2 ? vertices[i + 2].lm[j] : 0)); }
		}
	}
	GLshort shortVerts[2][2] = {{32767, 0}, {-32767, 16383}};
	glVertexPointer(2, GL_SHORT, 0, shortVerts); arrColor.enabled = 0; arrTexCoord[0].enabled = arrTexCoord[1].enabled = 0;
	repackVerts(0, 2); assert(scratchVerts[0] == 1 && scratchVerts[2] == 0 && scratchVerts[11] == -1);
	glClientActiveTextureARB(GL_TEXTURE0);
}

static void benchmarkRepacking(void)
{
	static GLfloat xyz[4096][4], uv[4096][2], lm[4096][2]; static GLubyte rgba[4096][4];
	for (int i = 0; i < 4096; i++) { xyz[i][0] = i; rgba[i][0] = i & 255; }
	glVertexPointer(3, GL_FLOAT, sizeof(xyz[0]), xyz); glColorPointer(4, GL_UNSIGNED_BYTE, 0, rgba);
	glEnableClientState(GL_COLOR_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, 0, uv); glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glClientActiveTextureARB(GL_TEXTURE1); glTexCoordPointer(2, GL_FLOAT, 0, lm); glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	for (int i = 0; i < 100; i++) repackVerts(0, 4096);
	double begin = emscripten_get_now();
	for (int i = 0; i < 3000; i++) repackVerts(0, 4096);
	printf("Vertex packing: %.2f ms for 12,288,000 vertices (CPU-only sample).\n", emscripten_get_now() - begin);
	assert(scratchVerts[4095 * 11] == 4095);
}

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
	checkRepacking(); benchmarkRepacking();
	puts("Performance: direct index uploads (24/12/6 bytes, zero index scratch allocations), 60/144 Hz pacing, cap changes and suspension recovery passed.");
	return 0;
}
