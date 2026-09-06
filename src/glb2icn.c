/*
 * glb2icn - PC-side offline converter: binary glTF (.glb) -> PS2 save icon (.icn)
 *
 * In the spirit of dcmesh: a single C executable, cgltf for GLB loading, stb for
 * the texture. Emits the animated-icon format the PS2 BIOS memory-card browser
 * renders (one static shape; a 128x128 BGR555 texture). The companion icon.sys
 * is written by the game at save time (playstation2.c) and references this file.
 *
 * Format reference: ps2savetools.com icon spec + babyno's .icn analysis.
 *   header(20) | vertices(count*24) | anim(20+16+8) | texture(128*128*2)
 *
 * Build:  make            (see Makefile / README for cgltf + stb paths)
 * Usage:  ./glb2icn [input.glb] [output.icn] [--cube|--flat] [--tex img]
 *                   [--scale F] [--flipy] [--noflipy] [--stretch] [--rgba R G B A]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#define ICN_TEX_DIM   128
#define ICN_FIX       4096.0f          /* fixed-point 1.0 */
#define ICN_VERT_MAX  1800             /* guardrail: a 4374-vert mesh was rejected by the
                                          BIOS as "Corrupted Data"; keep icons tiny (--flat) */

/* ---- little-endian output buffer ------------------------------------- */
typedef struct { uint8_t *p; size_t len, cap; } Buf;

static void buf_need(Buf *b, size_t n) {
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2 + 4096;
        b->p = (uint8_t *)realloc(b->p, b->cap);
        if (!b->p) { fprintf(stderr, "glb2icn: out of memory\n"); exit(1); }
    }
}
static void w8 (Buf *b, unsigned v){ buf_need(b,1); b->p[b->len++]=(uint8_t)v; }
static void w16(Buf *b, int v){ w8(b,v); w8(b,v>>8); }
static void w32(Buf *b, uint32_t v){ w8(b,v); w8(b,v>>8); w8(b,v>>16); w8(b,v>>24); }
static void wf (Buf *b, float f){ uint32_t u; memcpy(&u,&f,4); w32(b,u); }

static int clamp_s16(float v){ if(v> 32767.f)v= 32767.f; if(v<-32768.f)v=-32768.f; return (int)lrintf(v); }

/* ---- gathered geometry ----------------------------------------------- */
typedef struct { float x,y,z, nx,ny,nz, u,v; } Vtx;

static Vtx  *g_v   = NULL;
static size_t g_vn = 0, g_vcap = 0;

static void push_vtx(Vtx v){
    if (g_vn == g_vcap){ g_vcap = g_vcap? g_vcap*2 : 4096; g_v = realloc(g_v, g_vcap*sizeof(Vtx)); }
    g_v[g_vn++] = v;
}

/* column-major 4x4 (cgltf) * point / vector */
static void xform_pt (const float *m, const float *in, float *out){
    out[0]=m[0]*in[0]+m[4]*in[1]+m[8]*in[2]+m[12];
    out[1]=m[1]*in[0]+m[5]*in[1]+m[9]*in[2]+m[13];
    out[2]=m[2]*in[0]+m[6]*in[1]+m[10]*in[2]+m[14];
}
static void xform_vec(const float *m, const float *in, float *out){
    out[0]=m[0]*in[0]+m[4]*in[1]+m[8]*in[2];
    out[1]=m[1]*in[0]+m[5]*in[1]+m[9]*in[2];
    out[2]=m[2]*in[0]+m[6]*in[1]+m[10]*in[2];
}

static float *read_floats(const cgltf_accessor *a, int comps){
    float *o = malloc(a->count*comps*sizeof(float));
    for (cgltf_size i=0;i<a->count;i++) cgltf_accessor_read_float(a,i,&o[i*comps],comps);
    return o;
}

static void process_node(const cgltf_node *n){
    if (!n->mesh) return;
    float w[16]; cgltf_node_transform_world(n, w);
    for (cgltf_size pi=0; pi<n->mesh->primitives_count; pi++){
        const cgltf_primitive *pr = &n->mesh->primitives[pi];
        if (pr->type != cgltf_primitive_type_triangles) continue;
        const cgltf_accessor *pa=NULL,*na=NULL,*ta=NULL;
        for (cgltf_size i=0;i<pr->attributes_count;i++){
            cgltf_attribute_type t = pr->attributes[i].type;
            if (t==cgltf_attribute_type_position) pa=pr->attributes[i].data;
            else if (t==cgltf_attribute_type_normal && !na) na=pr->attributes[i].data;
            else if (t==cgltf_attribute_type_texcoord && !ta) ta=pr->attributes[i].data;
        }
        if (!pa) continue;
        float *pos = read_floats(pa,3);
        float *nor = na? read_floats(na,3):NULL;
        float *uv  = ta? read_floats(ta,2):NULL;
        cgltf_size icount = pr->indices ? pr->indices->count : pa->count;
        for (cgltf_size k=0;k<icount;k++){
            cgltf_size vi = pr->indices ? cgltf_accessor_read_index(pr->indices,k) : k;
            float ip[3]={pos[vi*3],pos[vi*3+1],pos[vi*3+2]}, op[3];
            xform_pt(w,ip,op);
            float on[3]={0,0,1};
            if (nor){ float in[3]={nor[vi*3],nor[vi*3+1],nor[vi*3+2]}; xform_vec(w,in,on);
                      float l=sqrtf(on[0]*on[0]+on[1]*on[1]+on[2]*on[2]); if(l>1e-6f){on[0]/=l;on[1]/=l;on[2]/=l;} }
            Vtx v; v.x=op[0]; v.y=op[1]; v.z=op[2];
            v.nx=on[0]; v.ny=on[1]; v.nz=on[2];
            v.u = uv? uv[vi*2]   : 0.f;
            v.v = uv? uv[vi*2+1] : 0.f;
            push_vtx(v);
        }
        free(pos); free(nor); free(uv);
    }
}

/* ---- texture: first material's base-color image -> 128x128 BGR555 ----- */
static const cgltf_image *find_base_image(const cgltf_data *d){
    if (!d) return NULL;
    for (cgltf_size m=0;m<d->materials_count;m++){
        const cgltf_material *mat=&d->materials[m];
        if (mat->has_pbr_metallic_roughness &&
            mat->pbr_metallic_roughness.base_color_texture.texture &&
            mat->pbr_metallic_roughness.base_color_texture.texture->image)
            return mat->pbr_metallic_roughness.base_color_texture.texture->image;
    }
    return d->images_count ? &d->images[0] : NULL;
}

/* The icon texture is a FIXED 128x128. To keep a non-square source's aspect
   ratio we letterbox: scale to fit inside 128x128, center it, and fill the
   margins with `pad`. `stretch` restores the old fill-the-square behavior. */
static void write_texture(Buf *b, const cgltf_data *d, const char *texfile, const uint8_t pad[4], int stretch){
    unsigned char *rgba = malloc(ICN_TEX_DIM*ICN_TEX_DIM*4);   /* 128x128x4 */
    for (int i=0;i<ICN_TEX_DIM*ICN_TEX_DIM;i++) memcpy(&rgba[i*4],pad,4);

    unsigned char *src = NULL; int w=0,h=0,n=0;
    if (texfile){                                 /* --tex: standalone image file */
        src = stbi_load(texfile,&w,&h,&n,4);
        if (!src) fprintf(stderr,"glb2icn: cannot load --tex %s\n",texfile);
    } else {
        const cgltf_image *img = find_base_image(d);
        if (img && img->buffer_view){
            const cgltf_buffer_view *bv = img->buffer_view;
            const uint8_t *bytes = (const uint8_t*)bv->buffer->data + bv->offset;
            src = stbi_load_from_memory(bytes,(int)bv->size,&w,&h,&n,4);
        }
    }
    if (src){
        int fw=ICN_TEX_DIM, fh=ICN_TEX_DIM;
        if (!stretch && w>0 && h>0){                  /* fit preserving aspect ratio */
            double sw=(double)ICN_TEX_DIM/w, sh=(double)ICN_TEX_DIM/h, s=(sw<sh)?sw:sh;
            fw=(int)(w*s+0.5); if(fw<1)fw=1; else if(fw>ICN_TEX_DIM)fw=ICN_TEX_DIM;
            fh=(int)(h*s+0.5); if(fh<1)fh=1; else if(fh>ICN_TEX_DIM)fh=ICN_TEX_DIM;
        }
        unsigned char *fit = malloc((size_t)fw*fh*4);
        stbir_resize_uint8_linear(src,w,h,0, fit,fw,fh,0, STBIR_RGBA);
        int ox=(ICN_TEX_DIM-fw)/2, oy=(ICN_TEX_DIM-fh)/2;     /* center */
        for (int y=0;y<fh;y++)
            memcpy(&rgba[((size_t)(oy+y)*ICN_TEX_DIM+ox)*4], &fit[(size_t)y*fw*4], (size_t)fw*4);
        free(fit); stbi_image_free(src);
        fprintf(stderr,"glb2icn: texture %dx%d -> %dx%d %s in 128x128\n",
                w,h,fw,fh, stretch?"(stretched)":"(letterboxed)");
    } else {
        fprintf(stderr,"glb2icn: no texture source; using solid pad color\n");
    }
    /* The PS2 icon samples bottom-up relative to our row order (renders
       upside-down), and faces are wound so the front view mirrors U
       (renders flipped left-right) -- so flip both axes. */
    for (int y=0;y<ICN_TEX_DIM/2;y++){
        uint8_t tmp[ICN_TEX_DIM*4];
        uint8_t *ra=&rgba[(size_t)y*ICN_TEX_DIM*4];
        uint8_t *rb=&rgba[(size_t)(ICN_TEX_DIM-1-y)*ICN_TEX_DIM*4];
        memcpy(tmp,ra,sizeof(tmp)); memcpy(ra,rb,sizeof(tmp)); memcpy(rb,tmp,sizeof(tmp));
    }
    for (int y=0;y<ICN_TEX_DIM;y++)
        for (int x=0;x<ICN_TEX_DIM/2;x++){
            uint8_t *pa=&rgba[((size_t)y*ICN_TEX_DIM+x)*4];
            uint8_t *pb=&rgba[((size_t)y*ICN_TEX_DIM+(ICN_TEX_DIM-1-x))*4];
            for (int k=0;k<4;k++){ uint8_t t=pa[k]; pa[k]=pb[k]; pb[k]=t; }
        }
    for (int i=0;i<ICN_TEX_DIM*ICN_TEX_DIM;i++){
        int r=rgba[i*4]>>3, g=rgba[i*4+1]>>3, bl=rgba[i*4+2]>>3;
        w16(b,(bl<<10)|(g<<5)|r);     /* BGR555 (X=0) */
    }
    free(rgba);
}

int main(int argc, char **argv){
    const char *in=NULL,*out=NULL; char outbuf[1024];
    float scale_arg=0.f;            /* 0 = auto-fit */
    int flipy=1;                    /* glTF +Y up; PS2 browser shows it upright with Y negated */
    int flipy_set=0;                /* did the user pass --flipy/--noflipy? */
    int stretch=0;                  /* default: letterbox the texture (keep its AR) */
    int flat=0;                     /* --flat: ignore mesh, emit a textured card */
    int cube=0;                     /* --cube: ignore mesh, emit a textured cube */
    float aspectx=1.f;              /* --aspect: extra X stretch to counter display PAR */
    float yoff=0.f;                 /* --yoff: lift the model to the BIOS zoom pivot */
    const char *texfile=NULL;       /* --tex: texture from a standalone image file */
    uint8_t pad[4]={0,0,0,255};     /* letterbox margin / no-texture fill color */
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--scale") && i+1<argc) scale_arg=(float)atof(argv[++i]);
        else if (!strcmp(argv[i],"--flipy"))   { flipy=1; flipy_set=1; }
        else if (!strcmp(argv[i],"--noflipy")) { flipy=0; flipy_set=1; }
        else if (!strcmp(argv[i],"--stretch")) stretch=1;
        else if (!strcmp(argv[i],"--flat"))    flat=1;
        else if (!strcmp(argv[i],"--cube"))    cube=1;
        else if (!strcmp(argv[i],"--aspect") && i+1<argc) aspectx=(float)atof(argv[++i]);
        else if (!strcmp(argv[i],"--yoff")   && i+1<argc) yoff=(float)atof(argv[++i]);
        else if (!strcmp(argv[i],"--tex") && i+1<argc) texfile=argv[++i];
        else if (!strcmp(argv[i],"--rgba") && i+4<argc){ pad[0]=atoi(argv[i+1]);pad[1]=atoi(argv[i+2]);pad[2]=atoi(argv[i+3]);pad[3]=atoi(argv[i+4]); i+=4; }
        else if (argv[i][0]=='-'){ fprintf(stderr,"glb2icn: unknown option %s\n",argv[i]); return 2; }
        else if (!in) in=argv[i];
        else if (!out) out=argv[i];
    }
    /* --cube/--flat + --tex need no GLB: the first positional becomes the output. */
    if ((cube||flat) && texfile && !out && in){ out=in; in=NULL; }
    if (!in && !texfile){ fprintf(stderr,"usage: glb2icn input.glb [output.icn] [--flat|--cube] [--tex img] [--scale F] [--noflipy] [--stretch] [--rgba R G B A]\n"); return 2; }
    if ((flat||cube) && !flipy_set) flipy=0;   /* authored in icon space already */
    if (!out){
        if (!in){ fprintf(stderr,"glb2icn: an output path is required\n"); return 2; }
        const char *base=in;
        for (const char *p=in; *p; ++p) if (*p=='/' || *p=='\\') base=p+1;
        const char *dot=strrchr(base,'.');
        size_t stem=dot ? (size_t)(dot-in) : strlen(in);
        const char suffix[]=".icn";
        if (stem > sizeof(outbuf)-sizeof(suffix)){
            fprintf(stderr,"glb2icn: output path is too long\n"); return 2;
        }
        memcpy(outbuf,in,stem); memcpy(outbuf+stem,suffix,sizeof(suffix)); out=outbuf;
    }

    cgltf_options opt={0}; cgltf_data *d=NULL;
    if (in){
        if (cgltf_parse_file(&opt,in,&d)!=cgltf_result_success){ fprintf(stderr,"glb2icn: parse failed: %s\n",in); return 1; }
        if (cgltf_load_buffers(&opt,d,in)!=cgltf_result_success){ fprintf(stderr,"glb2icn: load buffers failed\n"); cgltf_free(d); return 1; }
    }

    if (cube){
        /* A textured cube (36 verts): every face shows the full image. Icons
           can't hold a real model (the BIOS rejects large ones as "Corrupted
           Data"), so this is the simple recognizable shape. */
        static const float qt[4][2]={{0, 0},{1, 0},{1, 1},{0, 1}};   /* TL TR BR BL */
        static const int   tri[6]={0,2,1, 0,3,2};   /* CCW seen from outside */
        static const float faces[6][4][3]={
            {{-1, 1, 1},{ 1, 1, 1},{ 1,-1, 1},{-1,-1, 1}},   /* +Z */
            {{ 1, 1,-1},{-1, 1,-1},{-1,-1,-1},{ 1,-1,-1}},   /* -Z */
            {{ 1, 1, 1},{ 1, 1,-1},{ 1,-1,-1},{ 1,-1, 1}},   /* +X */
            {{-1, 1,-1},{-1, 1, 1},{-1,-1, 1},{-1,-1,-1}},   /* -X */
            {{-1, 1,-1},{ 1, 1,-1},{ 1, 1, 1},{-1, 1, 1}},   /* +Y */
            {{-1,-1, 1},{ 1,-1, 1},{ 1,-1,-1},{-1,-1,-1}},   /* -Y */
        };
        static const float fn[6][3]={{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
        for (int fi=0;fi<6;fi++)
            for (int k=0;k<6;k++){ int i=tri[k];
                Vtx v={faces[fi][i][0],faces[fi][i][1],faces[fi][i][2],
                       fn[fi][0],fn[fi][1],fn[fi][2], qt[i][0],qt[i][1]};
                push_vtx(v); }
    } else if (flat){
        /* A double-sided textured card (12 verts). The icon texture carries the
           image; the mesh is trivial. Authored in icon space. */
        static const float qp[4][2]={{-1, 1},{1, 1},{1,-1},{-1,-1}};  /* TL TR BR BL */
        static const float qt[4][2]={{0, 0},{1, 0},{1, 1},{0, 1}};
        static const int   fA[6]={0,1,2, 0,2,3};   /* front  (+Z) */
        static const int   fB[6]={0,3,2, 0,2,1};   /* back   (-Z), reversed */
        for (int k=0;k<6;k++){ int i=fA[k]; Vtx v={qp[i][0],qp[i][1],0, 0,0, 1, qt[i][0],qt[i][1]}; push_vtx(v); }
        for (int k=0;k<6;k++){ int i=fB[k]; Vtx v={qp[i][0],qp[i][1],0, 0,0,-1, qt[i][0],qt[i][1]}; push_vtx(v); }
    } else {
        for (cgltf_size i=0;i<d->nodes_count;i++) process_node(&d->nodes[i]);
    }
    if (g_vn==0){ fprintf(stderr,"glb2icn: no triangle geometry found\n"); cgltf_free(d); return 1; }
    if (g_vn % 3){ fprintf(stderr,"glb2icn: vertex count %zu not a multiple of 3\n",g_vn); cgltf_free(d); return 1; }
    if (g_vn > ICN_VERT_MAX){
        fprintf(stderr,"glb2icn: ERROR %zu verts (> %d) -- the PS2 BIOS rejects large icons as\n"
                       "         \"Corrupted Data\". Use --flat to emit a textured card instead.\n",
                g_vn, ICN_VERT_MAX);
        cgltf_free(d); return 1;
    }

    /* center + auto-fit to the unit icon view (longest axis -> ~+/-1.0) */
    float mn[3]={1e30f,1e30f,1e30f}, mx[3]={-1e30f,-1e30f,-1e30f};
    for (size_t i=0;i<g_vn;i++){ float*p=&g_v[i].x;
        for(int a=0;a<3;a++){ if(p[a]<mn[a])mn[a]=p[a]; if(p[a]>mx[a])mx[a]=p[a]; } }
    float c[3]={(mn[0]+mx[0])/2,(mn[1]+mx[1])/2,(mn[2]+mx[2])/2};
    float ext=0; for(int a=0;a<3;a++){ float e=mx[a]-mn[a]; if(e>ext)ext=e; }
    float s = scale_arg>0.f ? scale_arg : (ext>1e-6f ? 2.0f/ext : 1.0f);

    Buf b={0};
    w32(&b,0x010000);      /* magic */
    w32(&b,1);             /* animation_shapes */
    w32(&b,0x07);          /* tex_type: uncompressed textured */
    w32(&b,0x3F800000);    /* fixed (1.0f) */
    w32(&b,(uint32_t)g_vn);/* vertex_count */
    for (size_t i=0;i<g_vn;i++){
        Vtx*v=&g_v[i];
        float px=(v->x-c[0])*s*aspectx, py=(v->y-c[1])*s, pz=(v->z-c[2])*s;
        float ny=v->ny;
        if (flipy){ py=-py; ny=-ny; }
        py += yoff;                  /* shift onto the BIOS camera target (see README) */
        w16(&b,clamp_s16(px*ICN_FIX)); w16(&b,clamp_s16(py*ICN_FIX)); w16(&b,clamp_s16(pz*ICN_FIX)); w16(&b,0);
        w16(&b,clamp_s16(v->nx*ICN_FIX)); w16(&b,clamp_s16(ny*ICN_FIX)); w16(&b,clamp_s16(v->nz*ICN_FIX)); w16(&b,0);
        w16(&b,clamp_s16(v->u*ICN_FIX)); w16(&b,clamp_s16(v->v*ICN_FIX));
        w8(&b,255); w8(&b,255); w8(&b,255); w8(&b,255);   /* vertex color: white (let texture/light show) */
    }
    /* animation: one static frame, one keyframe */
    w32(&b,0x01); w32(&b,1); wf(&b,1.0f); w32(&b,0); w32(&b,1);
    w32(&b,0); w32(&b,1); w32(&b,0); w32(&b,0);
    wf(&b,0.0f); wf(&b,0.0f);
    write_texture(&b,d,texfile,pad,stretch);

    FILE *f=fopen(out,"wb");
    if (!f){ fprintf(stderr,"glb2icn: cannot write %s\n",out); free(b.p); free(g_v); cgltf_free(d); return 1; }
    int write_ok=fwrite(b.p,1,b.len,f)==b.len;
    if (fclose(f)!=0) write_ok=0;
    if (!write_ok){
        fprintf(stderr,"glb2icn: failed writing %s\n",out);
        if (remove(out)!=0) fprintf(stderr,"glb2icn: could not remove incomplete output %s\n",out);
        free(b.p); free(g_v); cgltf_free(d); return 1;
    }
    fprintf(stderr,"glb2icn: wrote %s (%zu verts, %zu bytes)\n",out,g_vn,b.len);

    free(b.p); free(g_v); cgltf_free(d);
    return 0;
}
