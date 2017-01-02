build: build-docker build-b2f-cpp

build-docker:
	docker-compose build base

build-b2f-cpp:
	docker-compose run --rm --workdir=/app/src shell make

clean:
	rm -rf src/main

down:
	docker-compose down

shell:
	@docker-compose run --rm shell

up:
	docker-compose run --rm --service-ports web
