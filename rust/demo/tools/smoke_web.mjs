// Browser smoke checks for the shipped Wasm application, including camera input.
import {readFile, mkdir} from 'node:fs/promises';
import {fileURLToPath, pathToFileURL} from 'node:url';
import path from 'node:path';
import {spawn} from 'node:child_process';
import assert from 'node:assert/strict';

const root=fileURLToPath(new URL('..',import.meta.url));
const manifest=await readFile(path.join(root,'Cargo.toml'),'utf8');
const name=manifest.match(/^name = "(.+)-demo"/m)[1];
const moduleName=process.env.PLAYWRIGHT_MODULE;
const {chromium}=await import(moduleName ? pathToFileURL(moduleName).href : 'playwright');
const external=process.env.WEB_DEMO_URL;
const server=external ? null : spawn(process.env.PYTHON || 'python3',['-m','http.server','8080','--bind','127.0.0.1','--directory',path.join(root,'dist')],{stdio:'ignore'});
const url=external || 'http://127.0.0.1:8080/';
const browser=await chromium.launch({headless:true,...(process.env.CHROMIUM_PATH?{executablePath:process.env.CHROMIUM_PATH}:{}),args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
const output=path.join(root,'screenshots');await mkdir(output,{recursive:true});
const queries=name==='tinyverlet' ? Array.from({length:6},(_,scene)=>`?scene=${scene}&paused=1&warmup=80`) : name==='tinywfc' ? ['?complete=1&seed=3','?complete=1&seed=42'] : name==='tinysubdiv' ? ['?shape=Torus&level=3','?shape=Letter%20T&level=3'] : ['?paused=1','?gallery=1&paused=1'];
try {
    for(let index=0;index<queries.length;index++) {
        const page=await browser.newPage({viewport:{width:1180,height:780},deviceScaleFactor:1});
        const errors=[];
        page.on('pageerror',error=>errors.push(error.message));
        page.on('console',message=>{if(message.type()==='error') errors.push(message.text());});
        for(let retry=0;;retry++) {try {await page.goto(url+queries[index]);break;} catch(error) {if(retry>=20)throw error;await new Promise(resolve=>setTimeout(resolve,150));}}
        await page.waitForFunction(()=>document.querySelector('#canvas')?.getAttribute('data-ready')==='true',null,{timeout:30000});
        await page.waitForTimeout(250);
        const before=await page.screenshot({path:path.join(output,`${name}-${index}.png`)});
        if(name!=='tinyeasings') {
            await page.mouse.move(800,340);await page.mouse.down();await page.mouse.move(920,400,{steps:12});await page.mouse.up();
            await page.mouse.move(1150,760);await page.waitForTimeout(150);
            const after=await page.screenshot();assert(!before.equals(after),'orbit camera must change the rendered scene');
        } else if(index===0) {
            await page.mouse.click(70,349);await page.mouse.move(1150,760);await page.waitForTimeout(150);
            assert(!before.equals(await page.screenshot({path:path.join(output,'scrubbed.png')})),'scrubbing progress must change the curve marker');
        }
        if(name==='tinysubdiv'&&index===0) {
            await page.evaluate(()=>{const data=new DataTransfer();data.items.add(new File(['v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n'],'triangle.obj',{type:'text/plain'}));document.querySelector('#canvas').dispatchEvent(new DragEvent('drop',{dataTransfer:data,bubbles:true,cancelable:true}));});
            await page.waitForTimeout(250);await page.screenshot({path:path.join(output,'obj-import.png')});
        }
        const state=await page.evaluate(()=>{const canvas=document.querySelector('#canvas');const gl=canvas.getContext('webgl2');return {width:canvas.width,height:canvas.height,error:gl.getError()};});
        assert(state.width>0&&state.height>0,'canvas must have a drawable surface');assert.equal(state.error,0,'renderer must leave no WebGL errors');assert.deepEqual(errors,[],`browser errors: ${errors.join('\n')}`);
        await page.close();
    }
    console.log(`${name}: ${queries.length} rendered scenarios and input checks passed`);
} finally {await browser.close();server?.kill();}
