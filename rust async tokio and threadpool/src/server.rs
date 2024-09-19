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
                    let sem_clone = sem.clone();
                    let response = Self::get_task_value(line, sem_clone).await;
                    if let Some(r) = response {
                        stream.write(&[r]).await.unwrap();
                    }
                }
                Err(e) => {
                    eprintln!("Unable to get command due to: {}", e);
                    return;
                }
            }
        }
    }

    async fn get_task_value(buf: String, sem: Arc<Semaphore>) -> Option<u8> {
        match Self::try_parse(buf, sem).await {
            Ok(r) => Some(r),
            Err(_) => None
        }
    }

    async fn try_parse(buf: String, sem: Arc<Semaphore>) -> Result<u8, Box<dyn std::error::Error>> {
        let numbers: Vec<&str> = buf.trim().split(':').collect();
        let task_type = numbers.first().unwrap().parse::<u8>()?;
        let seed = numbers.last().unwrap().parse::<u64>()?;

        if TaskType::from_u8(task_type).unwrap() == TaskType::CpuIntensiveTask {
            let _permit = sem.acquire().await.unwrap();
        }
        let result = Task::execute_async(task_type, seed).await;
        Ok(result)
    }
}
