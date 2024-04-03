#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include "protocol.pb.h"
// to compile:
// g++ client.cpp protocol.pb.cc -o clientx -lprotobuf -lpthread

using namespace std;
std::unordered_map<string,thread> privates;

void listenPrivateMessages(int sockfd,string username){
    while (true)
    {
        char buffer[8192];
        int bytes_recibidos = recv(sockfd, buffer, 8192, 0);
        if (bytes_recibidos == -1) {
            perror("recv fallido");
            break;
        } else if (bytes_recibidos == 0) {
            break;
        } else {
            chat::ServerResponse *respuesta=new chat::ServerResponse();
            if (respuesta->option()==4){ //este thread solo manejara mensajes privados del username
            if (respuesta->code() != 200) {
                std::cout << respuesta->servermessage()<< std::endl;
                break;
            }
            if (respuesta->messagecommunication().sender()==username){
                // Mostrar mensaje recibido
                cout <<"-------------------PRIVATE TRANSMISSION-------------------" << endl;
                cout << "Mensaje del servidor: " << respuesta->servermessage() << endl;
                cout<<respuesta->messagecommunication().sender() << ":\n"<< respuesta->messagecommunication().message() << endl;
                cout <<"------------------- END OF TRANSMISSION -------------------" << endl;
            }}
        }
    }
    
}
void listenResponses(int sockfd){
    while (true) {
        char buffer[8192];
        int bytes_recibidos = recv(sockfd, buffer, 8192, 0);
        if (bytes_recibidos == -1) {
            perror("recv fallido");
            break;
        } else if (bytes_recibidos == 0) {
            cout << "El servidor se desconectó." << endl;
            break;
        } else {
            // Parsear respuesta
            chat::ServerResponse *respuesta=new chat::ServerResponse();
            if (!respuesta->ParseFromString(buffer)) {
                cerr << "Fallo al parsear la respuesta.\n aborting" << endl;
                break;
            }
            // si hubo error
            if (respuesta->code() != 200) {
                std::cout << respuesta->servermessage()<< std::endl;
                return;
            }
            switch (respuesta->option())
            {
            case 2: //usarios conectados
                cout<<"---------------Retriieved Users:---------------"<<endl;
                for (int i = 0; i < respuesta->connectedusers().connectedusers_size(); ++i) {
                    auto user = respuesta->connectedusers().connectedusers(i);
                    cout<< "User: " << user.username()<<endl;
                }
                cout <<"-----------------------------------------------"<<endl ;
                break;
            case 3:
                cout<<"---------------STATUS CHANGE:---------------"<<endl;
                cout<<respuesta->servermessage()<<endl;
                cout<<"-------------END OF STATUS CHANGE:----------"<<endl;
            break;
            case 4: //este thread solo manejara mensajes publicos, no privados , solo imprime si no existe hilo para dicho chat con el user
            if (!respuesta->messagecommunication().has_recipient()||respuesta->messagecommunication().recipient()=="everyone"){
                // Mostrar mensaje recibido
                cout <<"-------------------INCOMMING GLOBAL TRANSMISSION-------------------" << endl;
                cout << "Mensaje del servidor: " << respuesta->servermessage() << endl;
                cout<<respuesta->messagecommunication().sender() << ":\n"<< respuesta->messagecommunication().message() << endl;
                cout <<"------------------- END OF TRANSMISSION -------------------" << endl;
            }
            else if(privates.find(respuesta->messagecommunication().sender()) == privates.end()){//no existe el thread
                cout <<"-------------------INCOMMING PRIVATE TRANSMISSION-------------------" << endl;
                cout << "Mensaje del servidor: " << respuesta->servermessage() << endl;
                cout<<respuesta->messagecommunication().sender() << ":\n"<< respuesta->messagecommunication().message() << endl;
                cout <<"------------------- END OF TRANSMISSION -------------------" << endl;
                //crear el thread para la transmision privada
                privates[respuesta->messagecommunication().sender()] = thread(listenPrivateMessages,sockfd,respuesta->messagecommunication().sender());
            }
            break;
            case 5:
                    cout<<"---------------USER INFO RETRIEVED:---------------"<<endl;
                    cout<<"USER: ";
                    cout<<respuesta->userinforesponse().username();
                    cout<<"\nSTATUS: ";
                    cout<<respuesta->userinforesponse().status();
                    cout<<"\nIP: ";
                    cout<<respuesta->userinforesponse().ip()<<endl;
                    cout<<"---------------END OF RETRIEVED-------------------"<<endl;
            break;
            default:
                cout<<"RECEIVED UNKNOWN RESPONSE OF SERVER"<<endl;
                break;
            }
        }
    }
}

void enviarMensaje(int sockfd, const string& mensaje, const string& remitente = "",const string& destinatario = "everyone") {
    char buffer[8192];
    chat::ClientPetition *request = new chat::ClientPetition();
    chat::MessageCommunication *mensaje_comunicacion = new chat::MessageCommunication();
    mensaje_comunicacion->set_message(mensaje);
    mensaje_comunicacion->set_recipient(destinatario);
    mensaje_comunicacion->set_sender(remitente);
    request->set_option(4);
    request->set_allocated_messagecommunication(mensaje_comunicacion);
    std::string mensaje_serializado;
    if(!request->SerializeToString(&mensaje_serializado)){
        cerr << "Fallo al serializar el mensaje." << endl;
        return;
    };
	strcpy(buffer, mensaje_serializado.c_str());
	send(sockfd, buffer, mensaje_serializado.size() + 1, 0);
}

void chateoPrivado(int sockfd, const string& destinatario,const string& remitente,const string& mensaje) {
    char buffer[8192];
    chat::ClientPetition *request = new chat::ClientPetition();
    chat::MessageCommunication *mensaje_comunicacion = new chat::MessageCommunication();
    mensaje_comunicacion->set_message(mensaje);
    mensaje_comunicacion->set_recipient(destinatario);
    mensaje_comunicacion->set_sender(remitente);
    request->set_option(4);
    request->set_allocated_messagecommunication(mensaje_comunicacion);
    std::string mensaje_serializado;
    if(!request->SerializeToString(&mensaje_serializado)){
        cerr << "Fallo al serializar el mensaje." << endl;
        return;
    };
	strcpy(buffer, mensaje_serializado.c_str());
	if(!send(sockfd, buffer, mensaje_serializado.size() + 1, 0)){perror("send failed");};
}

void cambiarEstado(int sockfd, const string& username, const string& estado) {
    char buffer[8192];
    chat::ClientPetition *request = new chat::ClientPetition();
    chat::ChangeStatus *cambio_estado = new chat::ChangeStatus;
    cambio_estado->set_username(username);
    cambio_estado->set_status(estado);
    request->set_option(3);
    request->set_allocated_change(cambio_estado);

    string cambio_estado_serializado;
    if (!request->SerializeToString(&cambio_estado_serializado)) {
        cerr << "Fallo al serializar el cambio de estado." << endl;
        return;
    }
    strcpy(buffer, cambio_estado_serializado.c_str());
    if (send(sockfd, buffer, cambio_estado_serializado.size()+1, 0) == -1) {
        perror("send fallido");
    }
}

void listarUsuarios(int sockfd) {
    char buffer[8192];
    chat::ClientPetition *peticion = new chat::ClientPetition();
    peticion->set_option(2); // Opción para listar usuarios conectados
    string peticion_serializada;
    if (!peticion->SerializeToString(&peticion_serializada)) {
        cerr << "Fallo al serializar la petición de lista de usuarios." << endl;
        return;
    }
    strcpy(buffer, peticion_serializada.c_str());
	if(!send(sockfd, buffer, peticion_serializada.size() + 1, 0)){perror("send fallido");};
}

void obtenerInfoUsuario(int sockfd, const string& nombre_usuario) {
   char buffer[8192];
    chat::ClientPetition *peticion = new chat::ClientPetition();
    string peticion_serializada;
    chat::UserRequest *user_req = new chat::UserRequest();
    user_req->set_user(nombre_usuario);
    peticion->set_option(5); // Opción para listar usuarios conectados
    peticion->set_allocated_users(user_req);
    if (!peticion->SerializeToString(&peticion_serializada)) {
        cerr << "Fallo al serializar la petición de usuario en específico." << endl;
        return;
    }
    strcpy(buffer, peticion_serializada.c_str());
    if(!send(sockfd, buffer, peticion_serializada.size() + 1, 0)){perror("send fallido");};    
}

int main(int argc, char const *argv[]) {
    if (argc != 4) {
        cerr << "Uso: " << argv[0] << " <nombredeusuario> <IPdelservidor> <puertodelservidor>" << endl;
        return 1;
    }

    string nombre_usuario = argv[1];
    string ip_servidor = argv[2];
    int puerto_servidor = stoi(argv[3]);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket fallido");
        return 1;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(puerto_servidor);
    inet_pton(AF_INET, ip_servidor.c_str(), &server_addr.sin_addr);

    if (connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect fallido");
        return 1;
    }

    std::string message_serialized;
    chat::ClientPetition *request = new chat::ClientPetition();
    chat::ServerResponse *response = new chat::ServerResponse();
    chat::UserRegistration *new_user = new chat::UserRegistration();

    new_user->set_ip("10.0.2.15");
    new_user->set_username(nombre_usuario);
    request->set_option(1);
    request->set_allocated_registration(new_user);
    char buffer[8192];

    request->SerializeToString(&message_serialized);
	strcpy(buffer, message_serialized.c_str());
	send(sockfd, buffer, message_serialized.size() + 1, 0);
	recv(sockfd, buffer, 8192, 0);
	response->ParseFromString(buffer);
	
	// si hubo error al buscar 
	if (response->code() != 200) {
		std::cout << response->servermessage()<< std::endl;
		return 0;
	}
    std::cout << "SERVER: "<< response->servermessage()<< std::endl;
    cout << "Conectado al servidor." << endl;
    
    // Iniciar hilo para recibir mensajes del servidor
    thread recibir_thread(listenResponses, sockfd);
    recibir_thread.detach();

    // Interacción con el usuario
    string opcion;
    while (true) {
        cout << "Seleccione una opción:" << endl;
        cout << "1. Enviar mensaje" << endl;
        cout << "2. Chatear privadamente" << endl;
        cout << "3. Cambiar de estado" << endl;
        cout << "4. Listar usuarios conectados" << endl;
        cout << "5. Obtener información de usuario en particular" << endl;
        cout << "6. Ayuda" << endl;
        cout << "7. Salir" << endl;
        cin >> opcion;

        if (opcion == "1") {
            string message;
            cout << "Ingresa tu mensaje:"<<endl;
            cin>>message;
            enviarMensaje(sockfd,message,nombre_usuario);
            // Código para enviar mensaje general
        } else if (opcion == "2") {
            // Código para chatear privadamente
            string destin;
            cout<<"Ingresa Username a enviar mensaje"<<endl;
            cin>>destin;
            string message;
            cout << "Ingresa tu mensaje:"<<endl;
            cin>>message;
            chateoPrivado(sockfd,destin,nombre_usuario,message);
            privates[destin] = thread(listenPrivateMessages,sockfd,destin);
        } else if (opcion == "3") {
            cout << "Seleccione un estado (ACTIVO, OCUPADO, INACTIVO): ";
            string estado;
            cin >> estado;
            cambiarEstado(sockfd, nombre_usuario,estado);
        } else if (opcion == "4") {
            listarUsuarios(sockfd);
        } else if (opcion == "5") {
            cout << "Ingrese el nombre del usuario: ";
            string nombre_usuario;
            cin >> nombre_usuario;
            obtenerInfoUsuario(sockfd, nombre_usuario);
        } else if (opcion == "6") {
            // Implementar ayuda
                // Mostrar mensaje de ayuda
                cout << "Bienvenido al sistema de chat." << endl;
                cout << endl;
                cout << "Este proyecto está basado en el propuesto por Bob Dugan y Erik Véliz en 2006. Con este trabajo se reforzarán los conocimientos sobre procesos, threads, concurrencia y comunicación entre procesos. El objetivo es desarrollar una aplicación de chat en C/C++." << endl;
                cout << endl;
                cout << "La aplicación consta de dos partes:" << endl;
                cout << "Servidor:" << endl;
                cout << "- Mantiene una lista de todos los clientes/usuarios conectados al sistema." << endl;
                cout << "- Sólo puede existir un servidor durante una ejecución del sistema de chat." << endl;
                cout << "- Se ejecuta como un proceso independiente mediante el siguiente comando: <nombredelservidor> <puertodelservidor>." << endl;
                cout << endl;
                cout << "Cliente:" << endl;
                cout << "- Se conecta y se registra con el servidor." << endl;
                cout << "- Permite chatear con otros usuarios, cambiar de estado, listar usuarios conectados, obtener información de usuario en particular, recibir ayuda y salir del programa." << endl;
                cout << "- Se ejecuta mediante el siguiente comando: <nombredelcliente> <nombredeusuario> <IPdelservidor> <puertodelservidor>." << endl;
                cout << endl;
                cout << "Opciones disponibles:" << endl;
                cout << "1. Chatear con todos los usuarios (broadcasting): Permite enviar un mensaje que será recibido por todos los usuarios conectados al sistema de chat." << endl;
                cout << "2. Enviar y recibir mensajes directos, privados, aparte del chat general: Permite enviar mensajes directamente a un usuario específico." << endl;
                cout << "3. Cambiar de status: ACTIVO, OCUPADO, INACTIVO: Permite cambiar el estado de tu sesión en el chat. Esto puede afectar cómo otros usuarios te ven." << endl;
                cout << "4. Listar los usuarios conectados al sistema de chat: Muestra una lista de todos los usuarios que están actualmente conectados al sistema de chat." << endl;
                cout << "5. Desplegar información de un usuario en particular: Permite obtener información detallada de un usuario específico, como su dirección IP." << endl;
                cout << "6. Ayuda: Muestra este mensaje de ayuda detallada sobre todas las opciones disponibles en el sistema de chat." << endl;
                cout << "7. Salir: Cierra la aplicación de chat y termina la conexión con el servidor." << endl;
                cout << endl;
                cout << "Para chatear con usuarios, el formato debe ser similar a lo siguiente: <usuario> <mensaje>." << endl;
                cout << endl;
                cout << "Recuerda que el servidor se ejecuta como un proceso independiente y debe estar activo para que los clientes se conecten y puedan comunicarse." << endl;
                cout << endl;
                cout << "¡Disfruta chateando!" << endl;
                cout << endl;
                cout << "Para chatear con usuarios, el formato debe ser similar a lo siguiente: <usuario> <mensaje>." << endl;
                cout << endl;
                cout << "Recuerda que el servidor se ejecuta como un proceso independiente y debe estar activo para que los clientes se conecten y puedan comunicarse." << endl;
                cout << endl;
                cout << "¡Disfruta chateando!" << endl;
        } else if (opcion == "7") {
            // Implementar salida
            break;
        } else {
            cout << "Opción no válida." << endl;
        }
    }

    // Cerrar socket
    close(sockfd);

    return 0;
}
