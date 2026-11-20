/* Shared studio color math; RGB bytes, hue degrees, saturation/lightness/value percentages. */
const ColorEngine = (() => {
  const clamp=(v,a=0,b=1)=>Math.min(b,Math.max(a,v));
  const wrap=h=>((h%360)+360)%360;
  const validRgb=c=>!!c && ['r','g','b'].every(k=>Number.isInteger(c[k])&&c[k]>=0&&c[k]<=255);
  function hexToRgb(hex){const m=typeof hex==='string'&&/^#?([\da-f]{6})$/i.exec(hex.trim());return m?{r:parseInt(m[1].slice(0,2),16),g:parseInt(m[1].slice(2,4),16),b:parseInt(m[1].slice(4),16)}:null}
  function rgbToHex(c){if(!validRgb(c))return null;return '#'+[c.r,c.g,c.b].map(n=>n.toString(16).padStart(2,'0')).join('').toUpperCase()}
  function geometry({r,g,b}){r/=255;g/=255;b/=255;const max=Math.max(r,g,b),min=Math.min(r,g,b),d=max-min;return {max,min,d,h:d?wrap(60*(max===r?(g-b)/d:max===g?2+(b-r)/d:4+(r-g)/d)):0}}
  function rgbToHsv(c){const {max,d,h}=geometry(c);return {h,s:max?100*d/max:0,v:100*max}}
  function rgbToHsl(c){const {max,min,d,h}=geometry(c),l=(max+min)/2;return {h,s:d?100*d/(1-Math.abs(2*l-1)):0,l:100*l}}
  function compose(h,c,m){const n=wrap(h)/60,x=c*(1-Math.abs(n%2-1));const [r,g,b]=[[c,x,0],[x,c,0],[0,c,x],[0,x,c],[x,0,c],[c,0,x]][Math.floor(n)];return {r:Math.round(255*clamp(r+m)),g:Math.round(255*clamp(g+m)),b:Math.round(255*clamp(b+m))}}
  function hsvToRgb({h,s,v}){s=clamp(s,0,100)/100;v=clamp(v,0,100)/100;return compose(h,v*s,v*(1-s))}
  function hslToRgb({h,s,l}){s=clamp(s,0,100)/100;l=clamp(l,0,100)/100;const c=(1-Math.abs(2*l-1))*s;return compose(h,c,l-c/2)}
  const lerp=(a,b,t)=>a+(b-a)*t;
  function mix(a,b,t,mode='rgb'){t=clamp(t);if(t===0)return {...a};if(t===1)return {...b};if(mode==='rgb')return {r:Math.round(lerp(a.r,b.r,t)),g:Math.round(lerp(a.g,b.g,t)),b:Math.round(lerp(a.b,b.b,t))};const x=rgbToHsv(a),y=rgbToHsv(b);if(!x.s)x.h=y.h;if(!y.s)y.h=x.h;let d=y.h-x.h;if(mode!=='hsv'){d=wrap(d+180)-180;if(mode==='longest'&&d)d+=d>0?-360:360}return hsvToRgb({h:x.h+d*t,s:lerp(x.s,y.s,t),v:lerp(x.v,y.v,t)})}
  function gradient(config,t){const stops=config.stops.filter(s=>s.enabled).sort((a,b)=>a.position-b.position);if(!stops.length)return {r:0,g:0,b:0};t=100*clamp(((config.reverse?1-t:t)-.5)*100/config.scale+config.position/100);if(t<stops[0].position)return stops[0].color;for(let i=1;i<stops.length;i++){const a=stops[i-1],b=stops[i];if(t<b.position)return mix(a.color,b.color,(t-a.position)/(b.position-a.position),config.interpolation)}return stops.at(-1).color}
  function scale(c,n){n=clamp(n,0,255);return {r:Math.floor(c.r*n/255),g:Math.floor(c.g*n/255),b:Math.floor(c.b*n/255)}}
  function ease(t,curve){t=clamp(t);return curve==='in'?t*t:curve==='out'?1-(1-t)**2:curve==='inout'?(t<.5?2*t*t:1-2*(1-t)**2):t}
  return {clamp,wrap,validRgb,hexToRgb,rgbToHex,rgbToHsv,hsvToRgb,rgbToHsl,hslToRgb,mix,gradient,scale,ease};
})();
if(typeof module!=='undefined')module.exports=ColorEngine;
