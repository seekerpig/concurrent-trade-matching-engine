use std::error::Error;
use std::sync::{Arc, mpsc};
use tokio::io::{AsyncBufReadExt, BufReader, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::Semaphore;

use crate::task::{Task, TaskType};

pub trait ServerTrait {
    async fn start_server(
        &self,
        address: String,
        tx: mpsc::Sender<Result<(), Box<dyn Error + Send>>>,
    );
}

pub struct Server;

impl ServerTrait for Server {
    async fn start_server(
        &self,
        address: String,
        tx: mpsc::Sender<Result<(), Box<dyn Error + Send>>>,
    ) {
        println!("Starting the server");
        let listener = TcpListener::bind(address).await;

        match listener {
            Ok(_) => tx.send(Ok(())).unwrap(),
            Err(e) => {
                println!("here {}", e);
                tx.send(Err(Box::new(e))).unwrap();
                return;
            }
        }

        let semaphore = Arc::new(Semaphore::new(40));

        loop {
            let connection = listener.as_ref().unwrap().accept().await;
            match connection {
                Ok(_) => {
                    let (stream, _) = connection.unwrap();
                    let sem_clone = semaphore.clone();
                    tokio::spawn(async move {
                        Self::handle_connection(stream, sem_clone).await;
                    });
                },
                Err(e) => {
                    println!("connection error {}", e);
                    return;
                }
            }
        }
    }
}

impl Server {
    async fn handle_connection(mut stream: TcpStream, sem: Arc<Semaphore>) {
        loop {
            let mut buf_reader = BufReader::new(&mut stream);
            let mut line = String::new();
            match buf_reader.read_line(&mut line).await {
                Ok(0) => {
                    return;
                }
                Ok(_) => {
                    line = line.trim().to_string();
                    let idx = line.find(':').unwrap();
                    let task_type = line[..idx].parse::<u8>().unwrap();
                    let seed = line[idx+1..].parse::<u64>().unwrap();

                    if TaskType::from_u8(task_type).unwrap() == TaskType::CpuIntensiveTask {
                        let _permit = sem.acquire().await.unwrap();
                    }
                    let response = Task::execute_async(task_type, seed).await;
                    stream.write(&[response]).await.unwrap();
                }
                Err(e) => {
                    eprintln!("Unable to get command due to: {}", e);
                    return;
                }
            }
        }
    }
}
