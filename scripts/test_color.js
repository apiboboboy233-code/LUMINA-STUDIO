'use strict';
const assert=require('node:assert/strict');
const C=require('../studio/color');
const names=['000000','FFFFFF','FF0000','00FF00','0000FF','00FFFF','FF00FF','FFFF00','808080'];
for(const hex of names) assert.equal(C.rgbToHex(C.hexToRgb(hex.toLowerCase())), '#'+hex);
for(const bad of ['#abc','gg0000','00000000','-00000','',null]) assert.equal(C.hexToRgb(bad),null);
assert.equal(C.validRgb({r:256,g:0,b:0}),false);
assert.deepEqual(C.rgbToHsv(C.hexToRgb('FF0000')),{h:0,s:100,v:100});
assert.deepEqual(C.rgbToHsl(C.hexToRgb('0000FF')),{h:240,s:100,l:50});
let seed=42;
for(let i=0;i<10000;i++){
 seed=(Math.imul(1664525,seed)+1013904223)>>>0;
 const c={r:seed&255,g:(seed>>>8)&255,b:(seed>>>16)&255};
 assert.deepEqual(C.hsvToRgb(C.rgbToHsv(c)),c);
 assert.deepEqual(C.hslToRgb(C.rgbToHsl(c)),c);
}
const red=C.hexToRgb('FF0000'),blue=C.hexToRgb('0000FF');
for(const [mode,expected] of [['rgb','#800080'],['hsv','#00FF00'],['shortest','#FF00FF'],['longest','#00FF00']]) assert.equal(C.rgbToHex(C.mix(red,blue,.5,mode)),expected);
const gradient={stops:[{color:red,position:0,enabled:true},{color:blue,position:100,enabled:true}],position:50,scale:100,reverse:false,interpolation:'rgb'};
assert.deepEqual(C.gradient(gradient,0),red);
assert.deepEqual(C.gradient({...gradient,reverse:true},0),blue);
assert.equal(C.rgbToHex(C.gradient(gradient,.5)),'#800080');
assert.deepEqual(C.scale(red,64),{r:64,g:0,b:0});
for(const [curve,result] of [['linear',.25],['in',.0625],['out',.4375],['inout',.125]])assert.equal(C.ease(.25,curve),result);
console.log('Color vectors, 10,000 RGB round trips, gradients, scaling and easing passed');
