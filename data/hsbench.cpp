// Standalone copy of QgsHillshadeRenderer's CPU multi-directional path: sliding window (current) vs direct neighbours.
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <algorithm>
#include <cstring>
typedef uint32_t QRgb;
static inline QRgb qRgba(int r, int g, int b, int a) { return ((a & 0xffu) << 24) | ((r & 0xffu) << 16) | ((g & 0xffu) << 8) | (b & 0xffu); }
struct P { float cellXSize, cellYSize, sin127, cos225_127, cos127, square_z; double opacity; QRgb nodataColor; };
static inline bool near1(double a) { return std::fabs(a - 1.0) <= 4 * 2.220446049250313e-16; } // qgsDoubleNear default epsilon
static inline QRgb shade(double x11,double x12,double x13,double x21,double x22,double x23,double x31,double x32,double x33,const P&p){
  const double derX = ( ( x13 + x23 + x23 + x33 ) - ( x11 + x21 + x21 + x31 ) ) / ( 8 * p.cellXSize );
  const double derY = ( ( x31 + x32 + x32 + x33 ) - ( x11 + x12 + x12 + x13 ) ) / ( 8 * -p.cellYSize );
  double grayValue;
  const float xx = derX * derX; const float yy = derY * derY; const float xx_plus_yy = xx + yy;
  if ( xx_plus_yy == 0.0 ) grayValue = std::clamp( static_cast<float>( 1.0 + p.sin127*2 ), 0.0f, 255.0f );
  else {
    float v225 = p.sin127 + ( derX - derY ) * p.cos225_127; v225 = ( v225 <= 0.0 ) ? 0.0 : v225;
    float v270 = p.sin127 - derX * p.cos127; v270 = ( v270 <= 0.0 ) ? 0.0 : v270;
    float v315 = p.sin127 + ( derX + derY ) * p.cos225_127; v315 = ( v315 <= 0.0 ) ? 0.0 : v315;
    float v360 = p.sin127 - derY * p.cos127; v360 = ( v360 <= 0.0 ) ? 0.0 : v360;
    const float w225 = 0.5 * xx_plus_yy - derX * derY; const float w270 = xx; const float w315 = xx_plus_yy - w225; const float w360 = yy;
    const float c = ( ( w225 * v225 + w270 * v270 + w315 * v315 + w360 * v360 ) / xx_plus_yy ) / ( 1 + p.square_z * xx_plus_yy );
    grayValue = std::clamp( 1.0f + c, 0.0f, 255.0f );
  }
  const double a = p.opacity;
  if ( near1( a ) ) return qRgba( grayValue, grayValue, grayValue, 255 );
  return qRgba( a * grayValue, a * grayValue, a * grayValue, a * 255 );
}
// current QGIS structure
static void slidingWindow(const std::vector<double>&v,const std::vector<unsigned char>&nd,int w,int h,const P&p,QRgb*out){
  double pv[9]; bool n[9];
  auto val=[&](int r,int c,bool&x){ size_t i=size_t(r)*w+c; x=nd[i]; return v[i]; };
  for(int row=0;row<h;row++) for(int col=0;col<w;col++){
    int iUp=row-1,iDown=row+1; if(row==0) iUp=row; else if(row==h-1) iDown=row;
    if(col==0){ pv[0]=val(iUp,0,n[0]);pv[1]=pv[0];n[1]=n[0];pv[2]=pv[0];n[2]=n[0];
      pv[3]=val(row,0,n[3]);pv[4]=pv[3];n[4]=n[3];pv[5]=pv[3];n[5]=n[3];
      pv[6]=val(iDown,0,n[6]);pv[7]=pv[6];n[7]=n[6];pv[8]=pv[6];n[8]=n[6]; }
    else { pv[0]=pv[1];pv[1]=pv[2];pv[3]=pv[4];pv[4]=pv[5];pv[6]=pv[7];pv[7]=pv[8];n[0]=n[1];n[1]=n[2];n[3]=n[4];n[4]=n[5];n[6]=n[7];n[7]=n[8]; }
    if(col<w-1){ pv[2]=val(iUp,col+1,n[2]); pv[5]=val(row,col+1,n[5]); pv[8]=val(iDown,col+1,n[8]); }
    QRgb &o=out[size_t(row)*w+col];
    if(n[4]){ o=p.nodataColor; continue; }
    const double x22=pv[4];
    o=shade(n[0]?x22:pv[0],n[1]?x22:pv[1],n[2]?x22:pv[2],n[3]?x22:pv[3],x22,n[5]?x22:pv[5],n[6]?x22:pv[6],n[7]?x22:pv[7],n[8]?x22:pv[8],p);
  }
}
// direct neighbours: same values (edge column/row replicated like the sliding window does)
static void direct(const std::vector<double>&v,const std::vector<unsigned char>&nd,int w,int h,const P&p,QRgb*out){
  for(int row=0;row<h;row++){
    const int up=row==0?row:row-1, down=row==h-1?row:row+1;
    const double *ru=&v[size_t(up)*w], *rc=&v[size_t(row)*w], *rd=&v[size_t(down)*w];
    const unsigned char *nu=&nd[size_t(up)*w], *nc=&nd[size_t(row)*w], *ndn=&nd[size_t(down)*w];
    QRgb *o=out+size_t(row)*w;
    for(int col=0;col<w;col++){
      const int l=col==0?0:col-1, r=col==w-1?col:col+1;
      if(nc[col]){ o[col]=p.nodataColor; continue; }
      const double x22=rc[col];
      o[col]=shade(nu[l]?x22:ru[l],nu[col]?x22:ru[col],nu[r]?x22:ru[r],nc[l]?x22:rc[l],x22,nc[r]?x22:rc[r],ndn[l]?x22:rd[l],ndn[col]?x22:rd[col],ndn[r]?x22:rd[r],p);
    }
  }
}
int main(){
  const int w=1184,h=1184; std::vector<double> v(size_t(w)*h); std::vector<unsigned char> nd(v.size());
  uint32_t s=1; for(size_t i=0;i<v.size();i++){ s=s*1664525u+1013904223u; int r=i/w,c=i%w; v[i]=200*std::sin(r*0.013)+150*std::cos(c*0.011)+(s>>24)*0.05; nd[i]=(r<40&&c<300)||((s>>8)%997==0); }
  P p{25.0f,25.0f,127.0f*0.707f,-32.87f*1.06f,127.0f*1.06f,2.25f,0.55,0x00000000};
  std::vector<QRgb> a(v.size()),b(v.size());
  for(auto [name,fn]: {std::pair{"sliding window (QGIS now)",slidingWindow},std::pair{"direct neighbours",direct}}){
    double best=1e9; for(int k=0;k<7;k++){ auto t=std::chrono::steady_clock::now(); fn(v,nd,w,h,p,(name[0]=='s'?a:b).data()); best=std::min(best,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count()); }
    printf("%-28s %6.2f ms\n",name,best);
  }
  printf("identical: %s\n", memcmp(a.data(),b.data(),a.size()*4)==0?"yes":"NO");
}
