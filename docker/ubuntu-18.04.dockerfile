FROM ubuntu:18.04

ARG DEBIAN_FRONTEND=noninteractive

COPY setup.py setup.py
COPY prepare.py prepare.py
COPY dev-requirements.txt dev-requirements.txt
COPY requirements.txt requirements.txt

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
RUN source py36/bin/activate && python3 prepare.py

RUN git config --global user.email jenkins@email.com
RUN git config --global user.name jenkins
