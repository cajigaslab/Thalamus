const artifact = require('@actions/artifact').default
console.log(artifact)
const fs = require('fs');

const files = fs.readdirSync(process.argv[2]).map(f => 'dist/' + f)
console.log('Uploading ' + JSON.stringify(files))

artifact.uploadArtifact(
  process.argv[2],
  files,
  process.argv[2],
  {}
).then(() => console.log('DONE'))

