// Parse emitted code, including HTML minifier output, before signing an offline bundle.
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const build=process.argv[2]||path.resolve(__dirname,'../../build_wasm');
const html=fs.readFileSync(path.join(build,'etl.html'),'utf8');let scripts=0;
for(const match of html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/gi)) {
    if(match[1].trim()){new vm.Script(match[1],{filename:'etl.html:inline-'+(++scripts)});}
}
if(scripts!==2)throw Error('Expected the launcher and gated engine loader scripts');
for(const name of ['etl.js','sw.js'])if(fs.existsSync(path.join(build,name)))new vm.Script(fs.readFileSync(path.join(build,name),'utf8'),{filename:name});
console.log('Generated launcher, engine glue and worker JavaScript parsed successfully.');
