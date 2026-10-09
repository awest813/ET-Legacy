/* Real GPU regression checks. Serve the generated HTML locally.
 * emcc misc/web/test_render.c -Isrc/webgl -O2 -sMIN_WEBGL_VERSION=2
 *      -sMAX_WEBGL_VERSION=2 --shell-file misc/web/test_render_shell.html
 *      -o build_wasm/test_render.html
 */
#include "../../src/webgl/webgl_shim.c"
#include <emscripten.h>

static int failures, checks;
static void check(const char *name, int pass)
{
	checks++; if(!pass) failures++;
	printf("%s: %s\n",name,pass?"PASS":"FAIL");
	EM_ASM({document.getElementById('results').textContent += '\n' + UTF8ToString($0) + ': ' + ($1 ? 'PASS' : 'FAIL');},name,pass);
}
static void pixel(const char *name, int x, int y, int r, int g, int b, int a)
{
	GLubyte actual[4]; int expected[4]={r,g,b,a}, pass=1;
	glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);
	for(int i=0;i<4;i++) if(abs((int)actual[i]-expected[i])>2) pass=0;
	checks++; if(!pass) failures++;
	printf("%s: %s (%d,%d,%d,%d)\n",name,pass?"PASS":"FAIL",actual[0],actual[1],actual[2],actual[3]);
	EM_ASM({document.getElementById('results').textContent += '\n' + UTF8ToString($0) + ': ' + ($1 ? 'PASS' : 'FAIL');},name,pass);
}
static void quad(float z)
{
	glBegin(GL_QUADS);
	glMultiTexCoord2fARB(GL_TEXTURE0_ARB,.5f,.5f); glMultiTexCoord2fARB(GL_TEXTURE1_ARB,.5f,.5f);
	glVertex3f(-1,-1,z); glVertex3f(1,-1,z); glVertex3f(1,1,z); glVertex3f(-1,1,z);
	glEnd();
}
static void texture(int unit, const GLubyte *rgba)
{
	GLuint name;
	glActiveTextureARB(GL_TEXTURE0_ARB+unit); glGenTextures(1,&name); glBindTexture(GL_TEXTURE_2D,name);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
}
static void grayscaleTextures(void)
{
	const GLint formats[]={GL_LUMINANCE,GL_LUMINANCE8,GL_LUMINANCE16,
		GL_LUMINANCE_ALPHA,GL_LUMINANCE8_ALPHA8,GL_LUMINANCE16_ALPHA16};
	const GLubyte original[4]={96,32,224,64}, replacement[4]={160,224,32,128};
	GLuint name;
	glActiveTextureARB(GL_TEXTURE0_ARB);glGenTextures(1,&name);glBindTexture(GL_TEXTURE_2D,name);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	glEnable(GL_TEXTURE_2D);glColor4f(1,1,1,1);
	for(int i=0;i<6;i++) {
		char label[80];
		glTexImage2D(GL_TEXTURE_2D,0,formats[i],1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,original);
		snprintf(label,sizeof(label),"Grayscale format %x uploads without GL error",formats[i]);
		check(label,glGetError()==GL_NO_ERROR);
		quad(0);snprintf(label,sizeof(label),"Grayscale format %x preserves luminance/alpha",formats[i]);
		pixel(label,64,64,96,96,96,i<3?255:64);
		glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,replacement);
		quad(0);snprintf(label,sizeof(label),"Grayscale format %x subimage matches storage",formats[i]);
		pixel(label,64,64,160,160,160,i<3?255:128);
	}
	GLubyte strided[24]={0};strided[20]=173;strided[23]=64;
	glPixelStorei(GL_UNPACK_ROW_LENGTH,3);glPixelStorei(GL_UNPACK_SKIP_ROWS,1);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS,1);glPixelStorei(GL_UNPACK_ALIGNMENT,8);
	glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE8_ALPHA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,strided);
	quad(0);pixel("Grayscale upload respects row stride and skips",64,64,173,173,173,64);
	strided[20]=81;strided[23]=192;
	glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,strided);
	quad(0);pixel("Grayscale subimage respects row stride and skips",64,64,81,81,81,192);
	GLint value;int unpackPreserved=1;
	const GLenum unpackNames[]={GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_ALIGNMENT};
	const GLint unpackValues[]={3,1,1,8};
	for(int i=0;i<4;i++){gl3_GetIntegerv(unpackNames[i],&value);if(value!=unpackValues[i])unpackPreserved=0;}
	check("Grayscale conversion restores caller unpack state",unpackPreserved);
	glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);glPixelStorei(GL_UNPACK_ALIGNMENT,4);
	glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE8_ALPHA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
	glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,replacement);
	quad(0);pixel("Empty grayscale storage accepts later pixels",64,64,160,160,160,128);
	const GLubyte mipBase[16]={96,32,224,64,96,32,224,64,96,32,224,64,96,32,224,64};
	glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE8_ALPHA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,mipBase);
	glTexImage2D(GL_TEXTURE_2D,1,GL_LUMINANCE8_ALPHA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,original);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
	quad(0);pixel("Grayscale mip chain stays complete",64,64,96,96,96,64);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	/* Reusing storage for color must discard its previous grayscale mode. */
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,original);
	glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,replacement);
	quad(0);pixel("Color redefinition clears grayscale conversion",64,64,160,224,32,128);
	glDisable(GL_TEXTURE_2D);glDeleteTextures(1,&name);
	/* Capture into an engine-style RGB texture with a complete mip chain. */
	glColor4f(.25f,.5f,.75f,1);quad(0);
	glGenTextures(1,&name);glBindTexture(GL_TEXTURE_2D,name);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
	glCopyTexImage2D(GL_TEXTURE_2D,0,GL_RGB,0,0,128,128,0);
	glGenerateMipmapEXT(GL_TEXTURE_2D);
	glClear(GL_COLOR_BUFFER_BIT);glEnable(GL_TEXTURE_2D);glColor4f(1,1,1,1);quad(0);
	pixel("Framebuffer copy with mipmapped sampling",64,64,64,128,191,255);
	check("Grayscale and framebuffer copy have no GL errors",glGetError()==GL_NO_ERROR);
	glDisable(GL_TEXTURE_2D);glDeleteTextures(1,&name);
}
int main(void)
{
	EmscriptenWebGLContextAttributes attrs;
	emscripten_webgl_init_context_attributes(&attrs);
	attrs.majorVersion=2; attrs.alpha=EM_TRUE; attrs.premultipliedAlpha=EM_FALSE;
	attrs.antialias=EM_FALSE; attrs.depth=EM_TRUE; attrs.stencil=EM_TRUE; attrs.preserveDrawingBuffer=EM_TRUE;
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context=emscripten_webgl_create_context("#canvas",&attrs);
	if(context<=0 || emscripten_webgl_make_context_current(context)!=EMSCRIPTEN_RESULT_SUCCESS) return 1;
	glewInit(); glViewport(0,0,128,128); glClearColor(0,0,0,0);
	EM_ASM({document.getElementById('results').textContent='Pixel checks';});
	emscripten_glEnable(0xffff);
	check("Backend GL errors reach the engine",glGetError()==GL_INVALID_ENUM);
	check("Backend GL errors are consumed once",glGetError()==GL_NO_ERROR);
	const GLenum limits[]={GL_MAX_TEXTURE_SIZE,GL_MAX_RENDERBUFFER_SIZE,GL_MAX_SAMPLES};
	int limitsMatch=1;
	for(int i=0;i<3;i++) {GLint shim,actual;glGetIntegerv(limits[i],&shim);gl3_GetIntegerv(limits[i],&actual);if(shim!=actual || shim<=0) limitsMatch=0;}
	check("GPU limits match WebGL",limitsMatch);
	GLfloat anisotropy=1,actualAnisotropy=1;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT,&anisotropy);
	if(GLEW_EXT_texture_filter_anisotropic) gl3_GetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT,&actualAnisotropy);
	check("Anisotropic filtering capability",anisotropy>=1 && anisotropy==actualAnisotropy);
	glEnable(GL_ALPHA_TEST);glEnable(GL_FOG);glEnable(GL_CLIP_PLANE0);
	check("Shim enable queries",glIsEnabled(GL_ALPHA_TEST) && glIsEnabled(GL_FOG) && glIsEnabled(GL_CLIP_PLANE0));
	glDisable(GL_ALPHA_TEST);glDisable(GL_FOG);glDisable(GL_CLIP_PLANE0);
	check("Shim disable queries",!glIsEnabled(GL_ALPHA_TEST) && !glIsEnabled(GL_FOG) && !glIsEnabled(GL_CLIP_PLANE0));
	check("No backend errors from shim queries",emscripten_glGetError()==GL_NO_ERROR);
	const GLubyte diffuse[4]={128,64,32,255}, lightmap[4]={64,128,192,128};
	texture(0,diffuse); texture(1,lightmap);
	glColor4f(1,1,1,1); quad(0);
	pixel("Diffuse × lightmap",64,64,32,32,24,128);
	glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_ADD); glColor4f(1,1,1,.5f); quad(0);
	pixel("Additive texture retains alpha",64,64,192,192,224,64);
	glDisable(GL_TEXTURE_2D); glActiveTextureARB(GL_TEXTURE0_ARB); glDisable(GL_TEXTURE_2D);
	grayscaleTextures();
	const GLfloat fog[4]={.2f,.6f,.8f,1};
	glFogfv(GL_FOG_COLOR,fog); glFogf(GL_FOG_START,0); glFogf(GL_FOG_END,1); glFogi(GL_FOG_MODE,GL_LINEAR); glEnable(GL_FOG);
	glColor4f(.8f,.4f,.2f,.25f); quad(-.5f);
	pixel("Fog changes RGB, preserves alpha",64,64,128,128,128,64);
	glDisable(GL_FOG); glClear(GL_COLOR_BUFFER_BIT);
	const GLdouble plane[4]={1,0,0,-.25};
	glClipPlane(GL_CLIP_PLANE0,plane); glEnable(GL_CLIP_PLANE0); glColor4f(1,0,0,1); quad(0);
	pixel("Portal rejected side",24,64,0,0,0,0);
	pixel("Portal retained side",110,64,255,0,0,255);
	glDisable(GL_CLIP_PLANE0);
	glClear(GL_COLOR_BUFFER_BIT); glAlphaFunc(GL_GREATER,.5f); glEnable(GL_ALPHA_TEST);
	glColor4f(1,0,0,.25f);quad(0);pixel("Transparent cutout rejected",64,64,0,0,0,0);
	glColor4f(0,1,0,.75f);quad(0);pixel("Opaque cutout retained",64,64,0,255,0,191);
	glDisable(GL_ALPHA_TEST);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glEnable(GL_DEPTH_TEST);
	glColor4f(1,0,0,1);quad(-.5f);glColor4f(0,0,1,1);quad(.5f);
	pixel("Depth keeps nearer surface",64,64,255,0,0,255);glDisable(GL_DEPTH_TEST);
	const GLfloat indexedVerts[4][3]={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
	const GLuint indexedInts[6]={0,1,2,0,2,3};
	const GLushort indexedShorts[6]={0,1,2,0,2,3};
	const GLubyte indexedBytes[6]={0,1,2,0,2,3};
	glVertexPointer(3,GL_FLOAT,0,indexedVerts);glEnableClientState(GL_VERTEX_ARRAY);
	glClear(GL_COLOR_BUFFER_BIT);glColor4f(0,1,0,1);
	glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,indexedInts);
	pixel("Direct uint indices render",64,64,0,255,0,255);
	glClear(GL_COLOR_BUFFER_BIT);glColor4f(0,0,1,1);
	glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,indexedShorts);
	pixel("Direct ushort indices render",64,64,0,0,255,255);
	glClear(GL_COLOR_BUFFER_BIT);glColor4f(1,0,0,1);
	glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_BYTE,indexedBytes);
	pixel("Direct byte indices render",64,64,255,0,0,255);
	glDisableClientState(GL_VERTEX_ARRAY);
	GLuint framebuffer,color;glGenFramebuffersEXT(1,&framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER,framebuffer);
	glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
	check("Mapped framebuffer texture complete",glCheckFramebufferStatusEXT(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
	glBindTexture(GL_TEXTURE_2D,0); /* Avoid sampling the active color attachment. */
	glViewport(0,0,64,64);glColor4f(1,1,0,1);quad(0);pixel("Offscreen framebuffer renders",32,32,255,255,0,255);
	glBindFramebufferEXT(GL_FRAMEBUFFER,0);glDeleteFramebuffersEXT(1,&framebuffer);glDeleteTextures(1,&color);
	check("No GL errors before context restart",glGetError()==GL_NO_ERROR);
	/* A real new WebGL context must not inherit current color or fog/matrix state. */
	glColor4f(.2f,.3f,.4f,.5f);glFogf(GL_FOG_START,4);glEnable(GL_FOG);
	glMatrixMode(GL_TEXTURE);glLoadIdentity();glTranslatef(.5f,.5f,0);
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE fresh=emscripten_webgl_create_context("#restartcanvas",&attrs);
	if(fresh<=0 || emscripten_webgl_make_context_current(fresh)!=EMSCRIPTEN_RESULT_SUCCESS) return 1;
	glewInit();GLfloat textureMatrix[16];
	glMatrixMode(GL_TEXTURE);glGetFloatv(GL_TEXTURE_MATRIX,textureMatrix);glMatrixMode(GL_MODELVIEW);
	check("Fresh context texture matrix identity",textureMatrix[0]==1 && textureMatrix[5]==1 && textureMatrix[10]==1 && textureMatrix[15]==1 && textureMatrix[12]==0);
	GLfloat current[4];glGetFloatv(GL_CURRENT_COLOR,current);
	check("Fresh context restores current color",current[0]==1 && current[1]==1 && current[2]==1 && current[3]==1);
	glViewport(0,0,128,128);quad(0);pixel("Fresh context renders without stale fog",64,64,255,255,255,255);
	if(glGetError()!=GL_NO_ERROR) failures++;
	EM_ASM({var el=document.getElementById('results'); el.textContent += '\n' + $0 + ' checks; ' + $1 + ' failures'; el.dataset.failures=String($1); el.style.color=$1?'#f77':'#9e9';},checks,failures);
	return failures?1:0;
}
