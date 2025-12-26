# Lessons

## It's not that smart and forgets a lot

* Confidently wrong a lot
* Make the same mistakes repeatedly

## Explicitly tell it to retain and reuse library code

* It loves to start everything from scratch and rewrite things that work so they don't

## Maths is dodgy

* E.g. lighting shaders had problems with light directions and conventions, repeatedly, despite being told to share working code
* Conventions seem to be hard for it to remember. Spell them out a lot.

## Detail that plan

* Get it to plan
* Record the plan to start in a new session
* Dump details to readme and agents.md
* It still forgets things

## It won't make tests by default

* You have to ask it to
* Come up with self-test schemes for things that don't fit UT

## Tell it explicitly to produce and update plan artefacts

* And update readme, agents to they stay relevant
* Produce docs on structures, pipelines, etc

## It will try to solve an immediate problem when there are better high level solutions

* E.g. spent ages on coordinate conversions to do a 180 degree rotation that can just be done as a single matrix, or transforming coordinates

## it will solve an immediate problem with poor practices

* e.g. attempting to solve a memory leak by explicit release on a smart pointer. Code smell.

## Any comments, ideas that enter context will typically not be validated

* If you tell it something wrong, it will take it on faith unless there is an explicit test