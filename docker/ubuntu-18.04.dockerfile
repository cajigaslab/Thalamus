FROM ubuntu:18.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt update && apt -y install python3 python3-pip locales sudo virtualenv
RUN rm -rf /usr/share/python-wheels/setuptools-39.0.1-py2.py3-none-any.whl
RUN python3 -m pip install -U setuptools pip

RUN locale-gen en_US en_US.UTF-8
RUN update-locale LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8
ENV LANG=en_US.UTF-8
RUN echo DEBIAN_FRONTEND=noninteractive > /etc/environment

SHELL ["/bin/bash", "-c"]

RUN python3 -m virtualenv -p python3.6 py36
RUN source py36/bin/activate && pip install -U pip
RUN apt -y install clang-10 libclang-10-dev
RUN source py36/bin/activate && pip install -Uvv opencv-contrib-python
RUN update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-10 100
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-10 100

COPY prepare.py prepare.py
COPY requirements.txt requirements.txt
RUN adduser --shell /bin/bash builder
RUN source py36/bin/activate && python3 prepare.py --home /home/builder

USER builder
WORKDIR /home/builder

RUN git config --global user.email jenkins@email.com
RUN git config --global user.name jenkins

RUN curl -o actions-runner-linux-x64-2.319.1.tar.gz -L https://github.com/actions/runner/releases/download/v2.319.1/actions-runner-linux-x64-2.319.1.tar.gz
RUN tar xzf ./actions-runner-linux-x64-2.319.1.tar.gz

ARG REPO
RUN --mount=type=secret,id=TOKEN,mode=0444 cat /run/secrets/TOKEN && ./config.sh --url ${REPO} --replace --token $(cat /run/secrets/TOKEN)

RUN curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.1/insta.sh | bash
RUN nvm install 17
RUN nvm use 17 && npm install -g @actions/artifact

CMD ["./run.sh"]

