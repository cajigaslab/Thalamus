pipeline {
    agent none
    options {
        disableConcurrentBuilds()
    }

    stages {
        stage('Maybe Cancel Build') {
            agent {
                label 'linux'
            }
            environment {
                PYPI_PASSWORD = credentials('PYPI_PASSWORD')
                DEBIAN_FRONTEND = 'noninteractive'
                TZ = 'America/New_York'
                DOCKER_BUILDKIT = '1'
            } 
        
            steps {
                script {
                    println('one')
                    skip_ci = sh(script: 'git log --format=%B -n 1 HEAD', returnStdout: true).contains('[skip ci]')
                    println('skip_ci = ' + skip_ci)
                    if(skip_ci) {
                        currentBuild.result = 'ABORTED'
                        error('Skipping Build')
                    }
                }
            }
        }
        stage('Windows Build') {
            agent {
              label 'windows'
            }
            environment {
                PYPI_PASSWORD = credentials('PYPI_PASSWORD')
                DEBIAN_FRONTEND = 'noninteractive'
                TZ = 'America/New_York'
                DOCKER_BUILDKIT = '1'
            } 
            steps {
                bat 'git config --global --add safe.directory "*"'
                bat 'powershell -Command "git tag -l | %%{git tag -d $_}"'
                bat "call C:\\py310\\Scripts\\activate.bat && python bump.py patch"
                bat "git rev-parse HEAD > version-commit"
                stash includes: 'version-commit', name: "windows-version-commit"
                bat 'set'
                //bat "git pull file://%%USERPROFILE%%/workspace/bmbi " + env.GIT_BRANCH.replace('origin/', '')
                //sh 'rm -rf `find . -maxdepth 1 -not -name .git -not -name .`'
                //sh 'git reset --hard'
                bat 'cd'
                bat 'dir'
                dir ('dist') {
                    deleteDir()
                }
                bat "call \"C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat\" && call \"C:\\py310\\Scripts\\activate.bat\" && call cicd.bat"
                stash includes: 'dist/*.whl', name: "windows-whl"
            }
        }
        stage('Linux Build') {
            agent {
              label 'linux'
            }
            environment {
                PYPI_PASSWORD = credentials('PYPI_PASSWORD')
                DEBIAN_FRONTEND = 'noninteractive'
                TZ = 'America/New_York'
                DOCKER_BUILDKIT = '1'
            } 
        
            steps {
                sh 'git config --add safe.directory "*"'
                sh 'git tag -d $(git tag -l)'
                sh "python3 bump.py patch"
                sh "git rev-parse HEAD > version-commit"
                stash includes: 'version-commit', name: "linux-version-commit"
                sh 'env'
                //bat "git pull file://%%USERPROFILE%%/workspace/bmbi " + env.GIT_BRANCH.replace('origin/', '')
                //sh 'rm -rf `find . -maxdepth 1 -not -name .git -not -name .`'
                //sh 'git reset --hard'
                sh 'ls'
                dir ('dist') {
                    deleteDir()
                }
                sh 'bash cicd.bash py36'
                stash includes: 'dist/*.whl', name: "linux-whl"
            }
        }
        stage('Publish Builds') {
            environment {
                PYPI_PASSWORD = credentials('PYPI_PASSWORD')
                ROOTCRT = credentials('rootcrt')
                DEBIAN_FRONTEND = 'noninteractive'
                TZ = 'America/New_York'
                DOCKER_BUILDKIT = '1'
            } 
        
            when {
                expression {
                    env.GIT_BRANCH == 'master'
                }
            }
            agent {
              label 'linux'
            }
            stages {
                stage('Publish') {
                    steps {
                        dir ('dist') {
                            deleteDir()
                        }
                        unstash "windows-whl"
                        unstash "linux-whl"
                        sh 'dir dist'
                        sh "twine upload --repository-url https://165.123.11.76:8081 " + "dist/*".replaceAll(/[-.]/, "_") + " -u bijanadmin -p $PYPI_PASSWORD --cert $ROOTCRT"
                    }
                }
            }
        }
        stage('Commit New Tag') {
            agent {
                label 'linux'
            }
            environment {
                PYPI_PASSWORD = credentials('PYPI_PASSWORD')
                DEBIAN_FRONTEND = 'noninteractive'
                TZ = 'America/New_York'
                DOCKER_BUILDKIT = '1'
            } 
            when {
                expression {
                    env.GIT_BRANCH == 'master'
                }
            }
            steps {
                unstash "linux-version-commit"
                script {
                    version_commit = sh(script: 'cat version-commit', returnStdout: true).trim().readLines().last()
                    echo 'VERSION_COMMIT: ' + version_commit
                    checked_out = sh(script: 'git checkout ' + version_commit, returnStatus: true) == 0
                    if(!checked_out) {
                        sh(script: 'git tag -d $(git tag -l)')
                        sh(script: "python3 bump.py patch")
                    }
                }
                sh 'git branch -f ' + env.GIT_BRANCH.replace('origin/', '')
                withCredentials([sshUserPrivateKey(credentialsId: 'git', keyFileVariable: 'SSH_KEY')]) {
                  withEnv(['GIT_SSH_COMMAND=ssh -oStrictHostKeyChecking=no -i ' + env.SSH_KEY]) {
                    sh 'echo $GIT_SSH_COMMAND'
                    sh 'git push origin ' + env.GIT_BRANCH.replace('origin/', '')
                    sh 'git push origin --tags'
                  }
                }
            }
        }
    }
}
