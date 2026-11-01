// Desktop GPU capture/blur. The CPU implementation remains the fallback.
#if !XASH_GLES && !XASH_GL_STATIC
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif
static GLuint radarGPUProgram,radarGPUTextures[2],radarGPUFBO[3];
static int radarGPUSize,radarGPUAxis,radarGPURadius,radarGPUResolution;
static unsigned radarGPUCaptures,radarCPUFallbacks;
static qboolean radarGPUFailed;
static CVAR_DEFINE_AUTO( r_radar_gpu, "1", 0, "keep radar capture and blur on the GPU" );

static GLuint GL_RadarShader(GLenum type,const char *source)
{
 GLint ok=0,len=(GLint)Q_strlen(source);
 GLuint shader=pglCreateShaderObjectARB(type);
 pglShaderSourceARB(shader,1,(const GLcharARB **)&source,&len);
 pglCompileShaderARB(shader);pglGetObjectParameterivARB(shader,GL_OBJECT_COMPILE_STATUS_ARB,&ok);
 if(!ok){pglDeleteObjectARB(shader);return 0;}return shader;
}
static qboolean GL_RadarGPUInit(void)
{
 if(radarGPUProgram)return true;
 if(radarGPUFailed||!GL_Support(GL_SHADER_OBJECTS_EXT)||!GL_Support(GL_FRAMEBUFFER_BLIT_EXT))return false;
 const char *vertex="#version 120\nvoid main(){gl_Position=vec4(gl_Vertex.xy,0.0,1.0);}";
 // Decode exact RGBA8 integers and truncate after each pass, just like the
 // legacy CPU blur. Cropped edge windows do not repeat border pixels.
 const char *fragment="#version 120\nuniform sampler2D sourceImage; uniform vec2 axis; uniform int radius; uniform float resolution;\n"
  "void main(){vec2 c=gl_FragCoord.xy; vec4 sum=vec4(0.0); float count=0.0;"
  "for(int i=-radius;i<=radius;++i){vec2 q=c+axis*float(i);"
  "if(q.x>=0.0&&q.y>=0.0&&q.x<resolution&&q.y<resolution){"
  "sum+=floor(texture2D(sourceImage,q/resolution)*255.0+0.5);count+=1.0;}}"
  "gl_FragColor=floor(sum/count+0.0001)/255.0;}";
 GLuint vs=GL_RadarShader(GL_VERTEX_SHADER_ARB,vertex),fs=GL_RadarShader(GL_FRAGMENT_SHADER_ARB,fragment);
 GLint linked=0;
 if(vs&&fs)
 {
  radarGPUProgram=pglCreateProgramObjectARB();pglAttachObjectARB(radarGPUProgram,vs);pglAttachObjectARB(radarGPUProgram,fs);
  pglLinkProgramARB(radarGPUProgram);pglGetObjectParameterivARB(radarGPUProgram,GL_OBJECT_LINK_STATUS_ARB,&linked);
 }
 if(vs)pglDeleteObjectARB(vs);if(fs)pglDeleteObjectARB(fs);
 if(!linked)
 {
  if(radarGPUProgram)pglDeleteObjectARB(radarGPUProgram);radarGPUProgram=0;radarGPUFailed=true;
  gEngfuncs.Con_Printf("radar_gpu: shader unavailable, using CPU blur\n");return false;
 }
 radarGPUAxis=pglGetUniformLocationARB(radarGPUProgram,"axis");
 radarGPURadius=pglGetUniformLocationARB(radarGPUProgram,"radius");
 radarGPUResolution=pglGetUniformLocationARB(radarGPUProgram,"resolution");
 pglGenTextures(2,radarGPUTextures);pglGenFramebuffers(3,radarGPUFBO);
 return true;
}
static qboolean GL_RadarGPUProcess(int x,int y,int res,int radius,const byte *fixture,byte *readback)
{
 if(!r_radar_gpu.value||!GL_RadarGPUInit())return false;
 GLint oldRead,oldDraw,oldProgram,oldTexture,oldReadBuffer;
 const int oldUnit=glState.activeTMU;
 pglGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldRead);pglGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&oldDraw);
 pglGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);pglGetIntegerv(GL_READ_BUFFER,&oldReadBuffer);
 GL_SelectTexture(0);pglGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);
 pglPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_VIEWPORT_BIT|GL_SCISSOR_BIT|GL_PIXEL_MODE_BIT);
 pglDisable(GL_BLEND);pglDisable(GL_DEPTH_TEST);pglDisable(GL_STENCIL_TEST);pglDisable(GL_ALPHA_TEST);
 pglDisable(GL_SCISSOR_TEST);pglDisable(GL_CULL_FACE);pglDisable(GL_DITHER);
 if(glConfig.version_major>=3)pglDisable(GL_FRAMEBUFFER_SRGB);
 pglColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
 if(!i_radar_texture)pglGenTextures(1,&i_radar_texture);
 const GLuint textures[3]={radarGPUTextures[0],radarGPUTextures[1],i_radar_texture};
 qboolean ok=true;
 if(radarGPUSize!=res)
 {
  for(int i=0;i<3;++i)
  {
   pglBindTexture(GL_TEXTURE_2D,textures[i]);
   pglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,i==2?GL_LINEAR:GL_NEAREST);
   pglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,i==2?GL_LINEAR:GL_NEAREST);
   pglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,i==2?GL_CLAMP:GL_CLAMP_TO_EDGE);pglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,i==2?GL_CLAMP:GL_CLAMP_TO_EDGE);
   pglTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,res,res,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
   pglBindFramebuffer(GL_FRAMEBUFFER,radarGPUFBO[i]);
   pglFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[i],0);
   if(pglCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)ok=false;
  }
  if(ok)radarGPUSize=res;
 }
 if(ok)
 {
  if(fixture)
  {pglBindTexture(GL_TEXTURE_2D,textures[0]);pglTexSubImage2D(GL_TEXTURE_2D,0,0,0,res,res,GL_RGBA,GL_UNSIGNED_BYTE,fixture);}
  else
  {
   pglBindFramebuffer(GL_READ_FRAMEBUFFER,oldRead);pglReadBuffer(oldRead?GL_COLOR_ATTACHMENT0:GL_BACK);
   pglBindFramebuffer(GL_DRAW_FRAMEBUFFER,radarGPUFBO[0]);
   pglBlitFramebuffer(x,y,x+res,y+res,0,0,res,res,GL_COLOR_BUFFER_BIT,GL_NEAREST);
  }
  pglUseProgramObjectARB(radarGPUProgram);pglUniform1iARB(pglGetUniformLocationARB(radarGPUProgram,"sourceImage"),0);
  pglUniform1iARB(radarGPURadius,radius);pglUniform1fARB(radarGPUResolution,(float)res);pglViewport(0,0,res,res);
  for(int pass=0;pass<2;++pass)
  {
   pglBindFramebuffer(GL_FRAMEBUFFER,radarGPUFBO[pass+1]);pglDrawBuffer(GL_COLOR_ATTACHMENT0);
   pglBindTexture(GL_TEXTURE_2D,textures[pass]);pglUniform2fARB(radarGPUAxis,pass?0.0f:1.0f,pass?1.0f:0.0f);
   pglBegin(GL_QUADS);pglVertex2f(-1,-1);pglVertex2f(1,-1);pglVertex2f(1,1);pglVertex2f(-1,1);pglEnd();
  }
  if(readback){pglReadBuffer(GL_COLOR_ATTACHMENT0);pglReadPixels(0,0,res,res,GL_RGBA,GL_UNSIGNED_BYTE,readback);}
  ok=pglGetError()==GL_NO_ERROR;
 }
 pglBindFramebuffer(GL_READ_FRAMEBUFFER,oldRead);pglBindFramebuffer(GL_DRAW_FRAMEBUFFER,oldDraw);
 pglPopAttrib();pglReadBuffer(oldReadBuffer);pglUseProgramObjectARB(oldProgram);
 pglBindTexture(GL_TEXTURE_2D,oldTexture);GL_SelectTexture(oldUnit);
 if(!ok)radarGPUSize=0;
 return ok;
}
static void GL_RadarGPUFree(void)
{
 if(radarGPUProgram)pglDeleteObjectARB(radarGPUProgram);
 if(radarGPUTextures[0])pglDeleteTextures(2,radarGPUTextures);
 if(radarGPUFBO[0])pglDeleteFramebuffers(3,radarGPUFBO);
 radarGPUProgram=0;radarGPUSize=0;radarGPUFailed=false;
 memset(radarGPUTextures,0,sizeof(radarGPUTextures));memset(radarGPUFBO,0,sizeof(radarGPUFBO));
}
static void GL_RadarGPUTest_f(void)
{
 static byte source[RADAR_PIXEL_BUFFER_SIZE],expected[RADAR_PIXEL_BUFFER_SIZE],actual[RADAR_PIXEL_BUFFER_SIZE];
 const int sizes[]={64,256,378,512};unsigned cases=0,failed=0,seed=0x12345678;
 for(unsigned n=0;n<4;++n)
 {
  int size=sizes[n];size_t bytes=(size_t)size*size*4;
  for(size_t i=0;i<bytes;++i){seed=seed*1664525u+1013904223u;source[i]=(byte)(seed>>24);}
  const int radii[]={1,4,size/16,size/8};
  for(unsigned r=0;r<4;++r)
  {
   GL_ApplyBoxBlur(source,expected,size,size,radii[r]);++cases;
   if(!GL_RadarGPUProcess(0,0,size,radii[r],source,actual)||memcmp(expected,actual,bytes))++failed;
  }
 }
 gEngfuncs.Con_Printf("radar_gpu_test: cases=%u failed=%u captures=%u fallbacks=%u\n",cases,failed,radarGPUCaptures,radarCPUFallbacks);
}
#else
static void GL_RadarGPUFree(void){}
static qboolean GL_RadarGPUProcess(int x,int y,int res,int radius,const byte *fixture,byte *readback){return false;}
#endif
