FROM ubuntu

ARG DEBIAN_FRONTEND=noninteractive

RUN apt update && apt -y install locales curl libicu-dev adduser
RUN locale-gen en_US en_US.UTF-8
RUN update-locale LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8
ENV LANG=en_US.UTF-8
RUN echo DEBIAN_FRONTEND=noninteractive > /etc/environment

SHELL ["/bin/bash", "-c"]

RUN adduser --shell /bin/bash uploader
USER uploader
WORKDIR /home/uploader

RUN curl -o actions-runner-linux-x64-2.319.1.tar.gz -L https://github.com/actions/runner/releases/download/v2.319.1/actions-runner-linux-x64-2.319.1.tar.gz
RUN tar xzf ./actions-runner-linux-x64-2.319.1.tar.gz

ARG REPO
RUN --mount=type=secret,id=TOKEN,mode=0444 ./config.sh --url ${REPO} --replace --name uploader --token $(cat /run/secrets/TOKEN)

RUN curl -o install.sh https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.1/install.sh
RUN bash install.sh

CMD ["./run.sh"]

