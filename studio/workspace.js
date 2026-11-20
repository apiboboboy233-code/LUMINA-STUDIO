'use strict';
(function(){
  if(typeof window.Studio==='undefined') window.Studio={};
  
  // State
  let railSegments=24, currentColor={r:89,g:225,b:255}, primaryColor={r:89,g:225,b:255}, secondaryColor={r:255,g:255,b:255}, previousColor=null;
  let selectedStopIndex=null, gradientStops=[{color:{r:89,g:225,b:255},position:0,enabled:true},{color:{r:255,g:134,b:96},position:100,enabled:true}];
  let colorChangeHandlers=[], colorTimer;

  // DOM shortcuts
  const $=id=>document.getElementById(id);
  
  // ColorEngine facade (implemented by parent)
  const CE=()=>ColorEngine;

  // UI utilities
  function hex(rgb){return CE().rgbToHex(rgb)}
  function toHsv(rgb){return CE().rgbToHsv(rgb)}
  function fromHsv(hsv){return CE().hsvToRgb(hsv)}
  function clamp(v,min,max){return Math.max(min,Math.min(max,v))}
  
  // Rail rendering
  function renderRail(){
    const rail=$('ws-rail');
    // Use firmware frames if available from last refresh
    if(window.lastColorState&&window.lastColorState.frames&&window.lastColorState.frames.length){
      const frames=window.lastColorState.frames;
      railSegments=frames.length;
      rail.replaceChildren(...frames.map(c=>{
        const el=document.createElement('i');
        el.style.setProperty('--pixel',hex(c));
        return el;
      }));
    }else{
      // Fallback: render gradient or solid
      const ctx=gradientStops.length>1?buildGradientConfig():null;
      rail.replaceChildren(...Array.from({length:railSegments},(_, i)=>{
        const t=i/(railSegments-1), c=ctx?CE().gradient(ctx,t):currentColor, el=document.createElement('i');
        el.style.setProperty('--pixel',hex(c));
        return el;
      }));
    }
    $('ws-rail-value').textContent=hex(currentColor);
  }

  // Map + hue rendering
  function renderMap(){
    const {h,s,v}=toHsv(currentColor), map=$('ws-map'), cursor=map.querySelector('.ws-cursor');
    map.style.setProperty('--hue',hex(fromHsv({h,s:100,v:100})));
    cursor.style.setProperty('--x',s+'%');
    cursor.style.setProperty('--y',(100-v)+'%');
    $('ws-hue').value=h;
    $('ws-hue-value').textContent=h+'°';
  }
  
  // Fields rendering
  function renderFields(){
    const {h,s,v}=toHsv(currentColor);
    $('ws-hex').value=hex(currentColor).toUpperCase();
    $('ws-native').value=hex(currentColor);
    $('ws-r').value=currentColor.r;
    $('ws-g').value=currentColor.g;
    $('ws-b').value=currentColor.b;
    $('ws-h').value=h.toFixed(0);
    $('ws-s').value=s.toFixed(1);
    $('ws-v').value=v.toFixed(1);
  }

  // Swatches
  function renderSwatches(){
    $('ws-primary').querySelector('span').style.setProperty('--swatch',hex(primaryColor));
    $('ws-secondary').querySelector('span').style.setProperty('--swatch',hex(secondaryColor));
    if(previousColor)$('ws-previous').querySelector('span').style.setProperty('--swatch',hex(previousColor));
  }

  // Gradient
  function buildGradientConfig(){
    return{stops:gradientStops,interpolation:$('ws-interpolation').value,reverse:$('ws-reverse').checked,position:Number($('ws-position').value),scale:Number($('ws-scale').value)};
  }
  function renderGradient(){
    const grad=$('ws-gradient'), cfg=buildGradientConfig(), samples=120, stops=cfg.stops.filter(s=>s.enabled).sort((a,b)=>a.position-b.position).map(s=>`${hex(s.color)} ${s.position}%`);
    if(stops.length<2){grad.style.background='#273246';return}
    grad.style.background=`linear-gradient(to right, ${stops.join(', ')})`;
    $('ws-position-value').textContent=$('ws-position').value+'%';
  }
  function renderStops(){
    const ctr=$('ws-stops');
    ctr.replaceChildren(...gradientStops.map((stop,i)=>{
      const btn=document.createElement('button');
      btn.type='button';
      btn.setAttribute('aria-pressed',selectedStopIndex===i);
      btn.innerHTML=`<span style="--swatch:${hex(stop.color)}"></span>${stop.position}%${stop.enabled?'':' (off)'}`;
      btn.onclick=()=>{selectedStopIndex=i;renderStops();renderStopFields()};
      return btn;
    }));
  }
  function renderStopFields(){
    if(selectedStopIndex===null||!gradientStops[selectedStopIndex]){$('ws-stop-color').disabled=true;$('ws-stop-position').disabled=true;$('ws-stop-enabled').disabled=true;return}
    const stop=gradientStops[selectedStopIndex];
    $('ws-stop-color').disabled=false;
    $('ws-stop-position').disabled=false;
    $('ws-stop-enabled').disabled=false;
    $('ws-stop-color').value=hex(stop.color);
    $('ws-stop-position').value=stop.position;
    $('ws-stop-enabled').checked=stop.enabled;
  }

  // Paint everything
  function paint(){renderRail();renderMap();renderFields();renderSwatches();renderGradient();renderStops();renderStopFields()}

  // Color updates
  function setColor(rgb,final=true){
    if(final&&previousColor!==currentColor)previousColor={...currentColor};
    currentColor={...rgb};
    paint();
    if(final){
      colorChangeHandlers.forEach(fn=>fn(currentColor));
      if(demo){window.StudioLibrary?.recordDemo(currentColor);primaryColor={...currentColor};return}
      clearTimeout(colorTimer);
      colorTimer=setTimeout(async()=>{
        try{await Studio.request('color','POST',{...currentColor,record:true});primaryColor={...currentColor};note('Controller accepted color');window.StudioLibrary?.refresh()}
        catch(err){note('Color not confirmed: '+err.message,true)}
      },180);
    }
  }
  
  // Map interaction
  function mapPointer(e){
    const map=$('ws-map'), rect=map.getBoundingClientRect(), x=clamp((e.clientX-rect.left)/rect.width,0,1)*100, y=clamp((e.clientY-rect.top)/rect.height,0,1)*100, {h}=toHsv(currentColor);
    setColor(fromHsv({h,s:x,v:100-y}),false);
  }
  $('ws-map').addEventListener('pointerdown',e=>{e.preventDefault();mapPointer(e);$('ws-map').setPointerCapture(e.pointerId);$('ws-map').addEventListener('pointermove',mapPointer)});
  $('ws-map').addEventListener('pointerup',e=>{$('ws-map').releasePointerCapture(e.pointerId);$('ws-map').removeEventListener('pointermove',mapPointer);setColor(currentColor,true)});
  $('ws-map').addEventListener('keydown',e=>{
    const {h,s,v}=toHsv(currentColor), step=e.shiftKey?10:1;
    if(e.key==='ArrowLeft')setColor(fromHsv({h,s:clamp(s-step,0,100),v}));
    else if(e.key==='ArrowRight')setColor(fromHsv({h,s:clamp(s+step,0,100),v}));
    else if(e.key==='ArrowUp')setColor(fromHsv({h,s,v:clamp(v+step,0,100)}));
    else if(e.key==='ArrowDown')setColor(fromHsv({h,s,v:clamp(v-step,0,100)}));
    else return;
    e.preventDefault();
  });

  // Hue slider
  $('ws-hue').addEventListener('input',()=>{const {h,s,v}=toHsv(currentColor);setColor(fromHsv({h:Number($('ws-hue').value),s,v}),false)});
  $('ws-hue').addEventListener('change',()=>setColor(currentColor,true));

  // Native color picker
  $('ws-native').addEventListener('input',()=>setColor(CE().hexToRgb($('ws-native').value),false));
  $('ws-native').addEventListener('change',()=>setColor(currentColor,true));

  // Form submit (apply color)
  $('ws-color-form').addEventListener('submit',async e=>{
    e.preventDefault();
    let target=null;
    const hex=$('ws-hex').value.trim();
    if(hex.match(/^#?[0-9A-Fa-f]{6}$/)){target=CE().hexToRgb(hex)}
    else{
      const rgb={r:Number($('ws-r').value),g:Number($('ws-g').value),b:Number($('ws-b').value)};
      if([rgb.r,rgb.g,rgb.b].every(n=>n>=0&&n<=255)){target=rgb}
      else{
        const hsv={h:Number($('ws-h').value),s:Number($('ws-s').value),v:Number($('ws-v').value)};
        if(hsv.h>=0&&hsv.h<=360&&hsv.s>=0&&hsv.s<=100&&hsv.v>=0&&hsv.v<=100){target=fromHsv(hsv)}
      }
    }
    if(!target)return;
    setColor(target);
  });

  // Swatches
  $('ws-primary').addEventListener('click',()=>setColor(primaryColor));
  $('ws-secondary').addEventListener('click',()=>setColor(secondaryColor));
  $('ws-previous').addEventListener('click',()=>{if(previousColor)setColor(previousColor)});
  $('ws-store-secondary').addEventListener('click',()=>{secondaryColor={...currentColor};paint()});
  $('ws-swap').addEventListener('click',()=>{const t=primaryColor;primaryColor=secondaryColor;secondaryColor=t;setColor(primaryColor);paint()});

  // Gradient position slider
  $('ws-position').addEventListener('input',renderGradient);

  // Stop controls
  $('ws-stop-add').addEventListener('click',()=>{gradientStops.push({color:{...currentColor},position:50,enabled:true});selectedStopIndex=gradientStops.length-1;paint()});
  $('ws-stop-remove').addEventListener('click',()=>{if(selectedStopIndex!==null&&gradientStops.length>2){gradientStops.splice(selectedStopIndex,1);selectedStopIndex=null;paint()}});
  $('ws-stop-color').addEventListener('input',()=>{if(selectedStopIndex!==null){gradientStops[selectedStopIndex].color=CE().hexToRgb($('ws-stop-color').value);paint()}});
  $('ws-stop-position').addEventListener('input',()=>{if(selectedStopIndex!==null){gradientStops[selectedStopIndex].position=Number($('ws-stop-position').value);paint()}});
  $('ws-stop-enabled').addEventListener('change',()=>{if(selectedStopIndex!==null){gradientStops[selectedStopIndex].enabled=$('ws-stop-enabled').checked;paint()}});

  // Gradient controls
  $('ws-interpolation').addEventListener('change',renderGradient);
  $('ws-scale').addEventListener('input',renderGradient);
  $('ws-reverse').addEventListener('change',renderGradient);

  // Gradient apply
  $('ws-gradient-apply').addEventListener('click',async()=>{
    const cfg=buildGradientConfig(), rgb=currentColor;
    const payload={r:rgb.r,g:rgb.g,b:rgb.b,gradient:cfg,record:true};
    try{
      if(!demo){ window.lastColorState=await Studio.request('color','POST',payload); }
      paint();
      note('Gradient applied');
    }catch(err){note('Gradient failed: '+err.message,true)}
  });

  // Transition preview
  $('ws-transition-preview').addEventListener('click',()=>{
    const sample=$('ws-transition-sample'), style=$('ws-style').value, duration=Number($('ws-duration').value), curve=$('ws-curve').value;
    const start=hex(primaryColor), end=hex(currentColor);
    sample.style.background=start;
    sample.style.transition='none';
    requestAnimationFrame(()=>{
      sample.style.transition=style==='instant'?'none':`background ${duration}ms ${curve==='linear'?'linear':curve==='in'?'ease-in':curve==='out'?'ease-out':'ease-in-out'}`;
      sample.style.background=end;
    });
  });

  // Transition apply
  $('ws-transition-apply').addEventListener('click',async()=>{
    const trans={style:$('ws-style').value,duration:Number($('ws-duration').value),curve:$('ws-curve').value}, rgb=currentColor;
    const payload={r:rgb.r,g:rgb.g,b:rgb.b,transition:trans,record:true};
    try{
      if(!demo){ window.lastColorState=await Studio.request('color','POST',payload); }
      paint();
      note('Transition saved');
    }catch(err){note('Transition failed: '+err.message,true)}
  });

  // Note
  function note(msg,error=false){const el=$('ws-note');el.textContent=msg;el.dataset.error=error;setTimeout(()=>el.textContent='',error?6000:3500)}

  // Public API
  Studio.getColor=()=>({...currentColor});
  Studio.setColor=setColor;
  Studio.applyGradient=cfg=>{gradientStops=cfg.stops||gradientStops;$('ws-interpolation').value=cfg.interpolation||'rgb';$('ws-reverse').checked=!!cfg.reverse;$('ws-position').value=cfg.position??0;$('ws-scale').value=cfg.scale??100;paint()};
  Studio.onColor=fn=>{colorChangeHandlers.push(fn);return()=>{colorChangeHandlers=colorChangeHandlers.filter(h=>h!==fn)}};
  Studio.request=async(path,methodOrOptions,body)=>{
    if(demo)throw new Error('Demo mode: network disabled');
    if(methodOrOptions && typeof methodOrOptions==='object'){
      const {method='GET',body:optBody}=methodOrOptions;
      return await api(path,{method,headers:{'Content-Type':'application/json'},body:typeof optBody==='string'?optBody:JSON.stringify(optBody)});
    }
    const method=methodOrOptions||'GET';
    return await api(path,{method,headers:{'Content-Type':'application/json'},body:typeof body==='string'?body:JSON.stringify(body)});
  };
  Studio.refresh=async()=>{
    try{
      const state=await Studio.request('color');
      window.lastColorState=state;
      if(state.primary)primaryColor=state.primary;
      if(state.secondary)secondaryColor=state.secondary;
      if(state.previous)previousColor=state.previous;
      if(state.gradient){
        gradientStops=state.gradient.stops||gradientStops;
        $('ws-interpolation').value=state.gradient.interpolation||'rgb';
        $('ws-reverse').checked=!!state.gradient.reverse;
        $('ws-position').value=state.gradient.position??0;
        $('ws-scale').value=state.gradient.scale??100;
      }
      if(state.transition){
        $('ws-style').value=state.transition.style||'instant';
        $('ws-duration').value=state.transition.duration||0;
        $('ws-curve').value=state.transition.curve||'linear';
      }
      if(state.frames&&state.frames.length){
        railSegments=state.frames.length;
      }
      setColor(primaryColor,false);
      note('Studio synced');
    }catch(err){note('Sync failed: '+err.message,true)}
  };

  // Initialization hook for parent
  Studio.init=()=>paint();

  // Initial render if standalone
  if(document.readyState==='loading'){
    document.addEventListener('DOMContentLoaded',()=>paint());
  }else{
    paint();
  }
})();
